/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Rodrigo Moya
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

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include "gdm-xsettings-manager.h"
#include "xsettings-manager.h"

#define GDM_XSETTINGS_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_XSETTINGS_MANAGER, GdmXsettingsManagerPrivate))

#ifdef HAVE_XFT2
#define FONT_RENDER_DIR "/desktop/gnome/font_rendering"
#define FONT_ANTIALIASING_KEY FONT_RENDER_DIR "/antialiasing"
#define FONT_HINTING_KEY      FONT_RENDER_DIR "/hinting"
#define FONT_RGBA_ORDER_KEY   FONT_RENDER_DIR "/rgba_order"
#define FONT_DPI_KEY          FONT_RENDER_DIR "/dpi"

/* X servers sometimes lie about the screen's physical dimensions, so we cannot
 * compute an accurate DPI value.  When this happens, the user gets fonts that
 * are too huge or too tiny.  So, we see what the server returns:  if it reports
 * something outside of the range [DPI_LOW_REASONABLE_VALUE,
 * DPI_HIGH_REASONABLE_VALUE], then we assume that it is lying and we use
 * DPI_FALLBACK instead.
 *
 * See get_dpi_from_gconf_or_server() below, and also
 * https://bugzilla.novell.com/show_bug.cgi?id=217790
 */
#define DPI_FALLBACK 96
#define DPI_LOW_REASONABLE_VALUE 50
#define DPI_HIGH_REASONABLE_VALUE 500

#endif /* HAVE_XFT2 */

typedef struct _TranslationEntry TranslationEntry;
typedef void (* TranslationFunc) (GdmXsettingsManager *manager,
                                  TranslationEntry    *trans,
                                  GConfValue          *value);

struct _TranslationEntry {
        const char     *gconf_key;
        const char     *xsetting_name;

        GConfValueType  gconf_type;
        TranslationFunc translate;
};

struct GdmXsettingsManagerPrivate
{
        XSettingsManager **managers;
};

enum {
        PROP_0,
};

static void     gdm_xsettings_manager_class_init  (GdmXsettingsManagerClass *klass);
static void     gdm_xsettings_manager_init        (GdmXsettingsManager      *xsettings_manager);
static void     gdm_xsettings_manager_finalize    (GObject                  *object);

G_DEFINE_TYPE (GdmXsettingsManager, gdm_xsettings_manager, G_TYPE_OBJECT)

static gpointer manager_object = NULL;

static void
translate_bool_int (GdmXsettingsManager *manager,
                    TranslationEntry    *trans,
                    GConfValue          *value)
{
        int i;

        g_assert (value->type == trans->gconf_type);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], trans->xsetting_name,
                                           gconf_value_get_bool (value));
        }
}

static void
translate_int_int (GdmXsettingsManager *manager,
                   TranslationEntry    *trans,
                   GConfValue          *value)
{
        int i;

        g_assert (value->type == trans->gconf_type);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], trans->xsetting_name,
                                           gconf_value_get_int (value));
        }
}

static void
translate_string_string (GdmXsettingsManager *manager,
                         TranslationEntry    *trans,
                         GConfValue          *value)
{
        int i;

        g_assert (value->type == trans->gconf_type);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              trans->xsetting_name,
                                              gconf_value_get_string (value));
        }
}

static void
translate_string_string_toolbar (GdmXsettingsManager *manager,
                                 TranslationEntry    *trans,
                                 GConfValue          *value)
{
        int         i;
        const char *tmp;

        g_assert (value->type == trans->gconf_type);

        /* This is kind of a workaround since GNOME expects the key value to be
         * "both_horiz" and gtk+ wants the XSetting to be "both-horiz".
         */
        tmp = gconf_value_get_string (value);
        if (tmp && strcmp (tmp, "both_horiz") == 0) {
                tmp = "both-horiz";
        }

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              trans->xsetting_name,
                                              tmp);
        }
}

static TranslationEntry translations [] = {
        { "/desktop/gnome/peripherals/mouse/double_click",   "Net/DoubleClickTime",     GCONF_VALUE_INT,      translate_int_int },
        { "/desktop/gnome/peripherals/mouse/drag_threshold", "Net/DndDragThreshold",    GCONF_VALUE_INT,      translate_int_int },
        { "/desktop/gnome/gtk-color-palette",                "Gtk/ColorPalette",        GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/font_name",              "Gtk/FontName",            GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/gtk_key_theme",          "Gtk/KeyThemeName",        GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/toolbar_style",          "Gtk/ToolbarStyle",        GCONF_VALUE_STRING,   translate_string_string_toolbar },
        { "/desktop/gnome/interface/toolbar_icon_size",      "Gtk/ToolbarIconSize",     GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/can_change_accels",      "Gtk/CanChangeAccels",     GCONF_VALUE_BOOL,     translate_bool_int },
        { "/desktop/gnome/interface/cursor_blink",           "Net/CursorBlink",         GCONF_VALUE_BOOL,     translate_bool_int },
        { "/desktop/gnome/interface/cursor_blink_time",      "Net/CursorBlinkTime",     GCONF_VALUE_INT,      translate_int_int },
        { "/desktop/gnome/interface/gtk_theme",              "Net/ThemeName",           GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/gtk_color_scheme",       "Gtk/ColorScheme",         GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/gtk-im-preedit-style",   "Gtk/IMPreeditStyle",      GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/gtk-im-status-style",    "Gtk/IMStatusStyle",       GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/icon_theme",             "Net/IconThemeName",       GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/file_chooser_backend",   "Gtk/FileChooserBackend",  GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/interface/menus_have_icons",       "Gtk/MenuImages",          GCONF_VALUE_BOOL,     translate_bool_int },
        { "/desktop/gnome/interface/menubar_accel",          "Gtk/MenuBarAccel",        GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/peripherals/mouse/cursor_theme",   "Gtk/CursorThemeName",     GCONF_VALUE_STRING,   translate_string_string },
        { "/desktop/gnome/peripherals/mouse/cursor_size",    "Gtk/CursorThemeSize",     GCONF_VALUE_INT,      translate_int_int },
        { "/desktop/gnome/interface/show_input_method_menu", "Gtk/ShowInputMethodMenu", GCONF_VALUE_BOOL,     translate_bool_int },
        { "/desktop/gnome/interface/show_unicode_menu",      "Gtk/ShowUnicodeMenu",     GCONF_VALUE_BOOL,     translate_bool_int },
};

#ifdef HAVE_XFT2
static double
dpi_from_pixels_and_mm (int pixels,
                        int mm)
{
        double dpi;

        if (mm >= 1)
                dpi = pixels / (mm / 25.4);
        else
                dpi = 0;

        return dpi;
}

static double
get_dpi_from_x_server (void)
{
        GdkScreen *screen;
        double     dpi;

        screen = gdk_screen_get_default ();
        if (screen != NULL) {
                double width_dpi, height_dpi;

                width_dpi = dpi_from_pixels_and_mm (gdk_screen_get_width (screen), gdk_screen_get_width_mm (screen));
                height_dpi = dpi_from_pixels_and_mm (gdk_screen_get_height (screen), gdk_screen_get_height_mm (screen));

                if (width_dpi < DPI_LOW_REASONABLE_VALUE || width_dpi > DPI_HIGH_REASONABLE_VALUE
                    || height_dpi < DPI_LOW_REASONABLE_VALUE || height_dpi > DPI_HIGH_REASONABLE_VALUE) {
                        dpi = DPI_FALLBACK;
                } else {
                        dpi = (width_dpi + height_dpi) / 2.0;
                }
        } else {
                /* Huh!?  No screen? */

                dpi = DPI_FALLBACK;
        }

        return dpi;
}

static double
get_dpi_from_gconf_or_x_server (GConfClient *client)
{
        GConfValue *value;
        double      dpi;

        value = gconf_client_get_without_default (client, FONT_DPI_KEY, NULL);

        /* If the user has ever set the DPI preference in GConf, we use that.
         * Otherwise, we see if the X server reports a reasonable DPI value:  some X
         * servers report completely bogus values, and the user gets huge or tiny
         * fonts which are unusable.
         */

        if (value != NULL) {
                dpi = gconf_value_get_float (value);
                gconf_value_free (value);
        } else {
                dpi = get_dpi_from_x_server ();
        }

        return dpi;
}

typedef struct
{
        gboolean    antialias;
        gboolean    hinting;
        int         dpi;
        const char *rgba;
        const char *hintstyle;
} GnomeXftSettings;

static const char *rgba_types[] = { "rgb", "bgr", "vbgr", "vrgb" };

/* Read GConf settings and determine the appropriate Xft settings based on them
 * This probably could be done a bit more cleanly with gconf_string_to_enum
 */
static void
xft_settings_get (GConfClient      *client,
                  GnomeXftSettings *settings)
{
        char  *antialiasing;
        char  *hinting;
        char  *rgba_order;
        double dpi;

        antialiasing = gconf_client_get_string (client, FONT_ANTIALIASING_KEY, NULL);
        hinting = gconf_client_get_string (client, FONT_HINTING_KEY, NULL);
        rgba_order = gconf_client_get_string (client, FONT_RGBA_ORDER_KEY, NULL);
        dpi = get_dpi_from_gconf_or_x_server (client);

        settings->antialias = TRUE;
        settings->hinting = TRUE;
        settings->hintstyle = "hintfull";
        settings->dpi = dpi * 1024; /* Xft wants 1/1024ths of an inch */
        settings->rgba = "rgb";

        if (rgba_order) {
                int i;
                gboolean found = FALSE;

                for (i = 0; i < G_N_ELEMENTS (rgba_types) && !found; i++) {
                        if (strcmp (rgba_order, rgba_types[i]) == 0) {
                                settings->rgba = rgba_types[i];
                                found = TRUE;
                        }
                }

                if (!found) {
                        g_warning ("Invalid value for " FONT_RGBA_ORDER_KEY ": '%s'",
                                   rgba_order);
                }
        }

        if (hinting) {
                if (strcmp (hinting, "none") == 0) {
                        settings->hinting = 0;
                        settings->hintstyle = "hintnone";
                } else if (strcmp (hinting, "slight") == 0) {
                        settings->hinting = 1;
                        settings->hintstyle = "hintslight";
                } else if (strcmp (hinting, "medium") == 0) {
                        settings->hinting = 1;
                        settings->hintstyle = "hintmedium";
                } else if (strcmp (hinting, "full") == 0) {
                        settings->hinting = 1;
                        settings->hintstyle = "hintfull";
                } else {
                        g_warning ("Invalid value for " FONT_HINTING_KEY ": '%s'",
                                   hinting);
                }
        }

        if (antialiasing) {
                gboolean use_rgba = FALSE;

                if (strcmp (antialiasing, "none") == 0) {
                        settings->antialias = 0;
                } else if (strcmp (antialiasing, "grayscale") == 0) {
                        settings->antialias = 1;
                } else if (strcmp (antialiasing, "rgba") == 0) {
                        settings->antialias = 1;
                        use_rgba = TRUE;
                } else {
                        g_warning ("Invalid value for " FONT_ANTIALIASING_KEY " : '%s'",
                                   antialiasing);
                }

                if (!use_rgba) {
                        settings->rgba = "none";
                }
        }

        g_free (rgba_order);
        g_free (hinting);
        g_free (antialiasing);
}

static void
xft_settings_set_xsettings (GdmXsettingsManager *manager,
                            GnomeXftSettings    *settings)
{
        int i;
        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/Antialias", settings->antialias);
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/Hinting", settings->hinting);
                xsettings_manager_set_string (manager->priv->managers [i], "Xft/HintStyle", settings->hintstyle);
                xsettings_manager_set_int (manager->priv->managers [i], "Xft/DPI", settings->dpi);
                xsettings_manager_set_string (manager->priv->managers [i], "Xft/RGBA", settings->rgba);
        }
}

static gboolean
write_all (int         fd,
           const char *buf,
           gsize       to_write)
{
        while (to_write > 0) {
                gssize count = write (fd, buf, to_write);
                if (count < 0) {
                        if (errno != EINTR)
                                return FALSE;
                } else {
                        to_write -= count;
                        buf += count;
                }
        }

        return TRUE;
}

static void
child_watch_cb (GPid     pid,
                int      status,
                gpointer user_data)
{
        char *command = user_data;

        if (!WIFEXITED (status) || WEXITSTATUS (status)) {
                g_warning ("Command %s failed", command);
        }
}

static void
spawn_with_input (const char *command,
                  const char *input)
{
        char   **argv;
        int      child_pid;
        int      inpipe;
        GError  *error;
        gboolean res;

        argv = NULL;
        res = g_shell_parse_argv (command, NULL, &argv, NULL);
        if (! res) {
                g_warning ("Unable to parse command: %s", command);
                return;
        }

        error = NULL;
        res = g_spawn_async_with_pipes (NULL,
                                        argv,
                                        NULL,
                                        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                        NULL,
                                        NULL,
                                        &child_pid,
                                        &inpipe,
                                        NULL,
                                        NULL,
                                        &error);
        g_strfreev (argv);

        if (! res) {
                g_warning ("Could not execute %s: %s", command, error->message);
                g_error_free (error);

                return;
        }

        if (input != NULL) {
                if (! write_all (inpipe, input, strlen (input))) {
                        g_warning ("Could not write input to %s", command);
                }

                close (inpipe);
        }

        g_child_watch_add (child_pid, (GChildWatchFunc) child_watch_cb, (gpointer)command);
}

static void
xft_settings_set_xresources (GnomeXftSettings *settings)
{
        const char *command;
        GString    *add_string;
        char       *old_locale;

        command = "xrdb -nocpp -merge";

        add_string = g_string_new (NULL);
        old_locale = g_strdup (setlocale (LC_NUMERIC, NULL));

        setlocale (LC_NUMERIC, "C");
        g_string_append_printf (add_string,
                                "Xft.dpi: %f\n",
                                settings->dpi / 1024.0);
        g_string_append_printf (add_string,
                                "Xft.antialias: %d\n",
                                settings->antialias);
        g_string_append_printf (add_string,
                                "Xft.hinting: %d\n",
                                settings->hinting);
        g_string_append_printf (add_string,
                                "Xft.hintstyle: %s\n",
                                settings->hintstyle);
        g_string_append_printf (add_string,
                                "Xft.rgba: %s\n",
                                settings->rgba);

        spawn_with_input (command, add_string->str);

        g_string_free (add_string, TRUE);
        setlocale (LC_NUMERIC, old_locale);
        g_free (old_locale);
}

/* We mirror the Xft properties both through XSETTINGS and through
 * X resources
 */
static void
update_xft_settings (GdmXsettingsManager *manager,
                     GConfClient         *client)
{
        GnomeXftSettings settings;

        xft_settings_get (client, &settings);
        xft_settings_set_xsettings (manager, &settings);
        xft_settings_set_xresources (&settings);
}

static void
xft_callback (GConfClient         *client,
              guint                cnxn_id,
              GConfEntry          *entry,
              GdmXsettingsManager *manager)
{
        int i;

        update_xft_settings (manager, client);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }
}

#endif /* HAVE_XFT2 */

static const char *
type_to_string (GConfValueType type)
{
        switch (type) {
        case GCONF_VALUE_INT:
                return "int";
        case GCONF_VALUE_STRING:
                return "string";
        case GCONF_VALUE_FLOAT:
                return "float";
        case GCONF_VALUE_BOOL:
                return "bool";
        case GCONF_VALUE_SCHEMA:
                return "schema";
        case GCONF_VALUE_LIST:
                return "list";
        case GCONF_VALUE_PAIR:
                return "pair";
        case GCONF_VALUE_INVALID:
                return "*invalid*";
        default:
                g_assert_not_reached();
                return NULL; /* for warnings */
        }
}

static void
process_value (GdmXsettingsManager *manager,
               TranslationEntry    *trans,
               GConfValue          *val)
{
        if (val == NULL) {
                int i;

                for (i = 0; manager->priv->managers [i]; i++) {
                        xsettings_manager_delete_setting (manager->priv->managers [i], trans->xsetting_name);
                }
        } else {
                if (val->type == trans->gconf_type) {
                        (* trans->translate) (manager, trans, val);
                } else {
                        g_warning (_("GConf key %s set to type %s but its expected type was %s\n"),
                                   trans->gconf_key,
                                   type_to_string (val->type),
                                   type_to_string (trans->gconf_type));
                }
        }
}

gboolean
gdm_xsettings_manager_start (GdmXsettingsManager *manager,
                             GError             **error)
{
        GConfClient *client;
        int          i;

        g_debug ("Starting xsettings manager");

        client = gconf_client_get_default ();

        for (i = 0; i < G_N_ELEMENTS (translations); i++) {
                GConfValue *val;
                GError     *err;

                err = NULL;
                val = gconf_client_get (client,
                                        translations[i].gconf_key,
                                        &err);

                if (err != NULL) {
                        g_warning ("Error getting value for %s: %s\n",
                                   translations[i].gconf_key,
                                   err->message);
                        g_error_free (err);
                } else {
                        process_value (manager, &translations[i], val);
                        if (val != NULL) {
                                gconf_value_free (val);
                        }
                }
        }

        g_object_unref (client);

#ifdef HAVE_XFT2
        update_xft_settings (manager, client);
#endif /* HAVE_XFT */

        for (i = 0; manager->priv->managers [i]; i++)
                xsettings_manager_set_string (manager->priv->managers [i],
                                              "Net/FallbackIconTheme",
                                              "gnome");

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }

        return TRUE;
}

void
gdm_xsettings_manager_stop (GdmXsettingsManager *manager)
{
        g_debug ("Stopping xsettings manager");
}

static void
gdm_xsettings_manager_set_property (GObject        *object,
                                    guint           prop_id,
                                    const GValue   *value,
                                    GParamSpec     *pspec)
{
        GdmXsettingsManager *self;

        self = GDM_XSETTINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_xsettings_manager_get_property (GObject        *object,
                                    guint           prop_id,
                                    GValue         *value,
                                    GParamSpec     *pspec)
{
        GdmXsettingsManager *self;

        self = GDM_XSETTINGS_MANAGER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_xsettings_manager_constructor (GType                  type,
                                  guint                  n_construct_properties,
                                  GObjectConstructParam *construct_properties)
{
        GdmXsettingsManager      *xsettings_manager;
        GdmXsettingsManagerClass *klass;

        klass = GDM_XSETTINGS_MANAGER_CLASS (g_type_class_peek (GDM_TYPE_XSETTINGS_MANAGER));

        xsettings_manager = GDM_XSETTINGS_MANAGER (G_OBJECT_CLASS (gdm_xsettings_manager_parent_class)->constructor (type,
                                                                                                                  n_construct_properties,
                                                                                                                  construct_properties));

        return G_OBJECT (xsettings_manager);
}

static void
gdm_xsettings_manager_dispose (GObject *object)
{
        GdmXsettingsManager *xsettings_manager;

        xsettings_manager = GDM_XSETTINGS_MANAGER (object);

        G_OBJECT_CLASS (gdm_xsettings_manager_parent_class)->dispose (object);
}

static void
gdm_xsettings_manager_class_init (GdmXsettingsManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_xsettings_manager_get_property;
        object_class->set_property = gdm_xsettings_manager_set_property;
        object_class->constructor = gdm_xsettings_manager_constructor;
        object_class->dispose = gdm_xsettings_manager_dispose;
        object_class->finalize = gdm_xsettings_manager_finalize;

        g_type_class_add_private (klass, sizeof (GdmXsettingsManagerPrivate));
}

static TranslationEntry *
find_translation_entry (const char *gconf_key)
{
        int i;

        for (i =0; i < G_N_ELEMENTS (translations); i++) {
                if (strcmp (translations[i].gconf_key, gconf_key) == 0) {
                        return &translations[i];
                }
        }

        return NULL;
}

static void
xsettings_callback (GConfClient         *client,
                    guint                cnxn_id,
                    GConfEntry          *entry,
                    GdmXsettingsManager *manager)
{
        TranslationEntry *trans;
        int               i;

        trans = find_translation_entry (entry->key);
        if (trans == NULL) {
                return;
        }

        process_value (manager, trans, entry->value);

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_set_string (manager->priv->managers [i],
                                              "Net/FallbackIconTheme",
                                              "gnome");
        }

        for (i = 0; manager->priv->managers [i]; i++) {
                xsettings_manager_notify (manager->priv->managers [i]);
        }
}

static void
register_config_callback (GdmXsettingsManager  *manager,
                          const char           *path,
                          GConfClientNotifyFunc func)
{
        GConfClient *client;

        client = gconf_client_get_default ();

        gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_NONE, NULL);
        gconf_client_notify_add (client, path, func, manager, NULL, NULL);

        g_object_unref (client);
}

static void
terminate_cb (void *data)
{
        gboolean *terminated = data;

        if (*terminated) {
                return;
        }

        *terminated = TRUE;

        gtk_main_quit ();
}

static void
gdm_xsettings_manager_init (GdmXsettingsManager *manager)
{
        GdkDisplay *display;
        int         i;
        int         n_screens;
        gboolean    res;
        gboolean    terminated;

        manager->priv = GDM_XSETTINGS_MANAGER_GET_PRIVATE (manager);

        display = gdk_display_get_default ();
        n_screens = gdk_display_get_n_screens (display);

        res = xsettings_manager_check_running (gdk_x11_display_get_xdisplay (display),
                                               gdk_screen_get_number (gdk_screen_get_default ()));
        if (res) {
                g_error ("You can only run one xsettings manager at a time; exiting\n");
                exit (1);
        }

        manager->priv->managers = g_new (XSettingsManager *, n_screens + 1);

        terminated = FALSE;
        for (i = 0; i < n_screens; i++) {
                GdkScreen *screen;

                screen = gdk_display_get_screen (display, i);

                manager->priv->managers [i] = xsettings_manager_new (gdk_x11_display_get_xdisplay (display),
                                                                     gdk_screen_get_number (screen),
                                                                     terminate_cb,
                                                                     &terminated);
                if (! manager->priv->managers [i]) {
                        g_error ("Could not create xsettings manager for screen %d!\n", i);
                        exit (1);
                }
        }

        manager->priv->managers [i] = NULL;

        register_config_callback (manager, "/desktop/gnome/peripherals/mouse", (GConfClientNotifyFunc)xsettings_callback);
        register_config_callback (manager, "/desktop/gtk", (GConfClientNotifyFunc)xsettings_callback);
        register_config_callback (manager, "/desktop/gnome/interface", (GConfClientNotifyFunc)xsettings_callback);

#ifdef HAVE_XFT2
        register_config_callback (manager, FONT_RENDER_DIR, (GConfClientNotifyFunc)xft_callback);
#endif /* HAVE_XFT2 */

}

static void
gdm_xsettings_manager_finalize (GObject *object)
{
        GdmXsettingsManager *xsettings_manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_XSETTINGS_MANAGER (object));

        xsettings_manager = GDM_XSETTINGS_MANAGER (object);

        g_return_if_fail (xsettings_manager->priv != NULL);

        G_OBJECT_CLASS (gdm_xsettings_manager_parent_class)->finalize (object);
}

GdmXsettingsManager *
gdm_xsettings_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                manager_object = g_object_new (GDM_TYPE_XSETTINGS_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
        }

        return GDM_XSETTINGS_MANAGER (manager_object);
}
