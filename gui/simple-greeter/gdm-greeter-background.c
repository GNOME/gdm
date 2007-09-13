/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include <gconf/gconf-client.h>

#include "gdm-greeter-background.h"

#define GDM_GREETER_BACKGROUND_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_GREETER_BACKGROUND, GdmGreeterBackgroundPrivate))

typedef enum {
        COLOR_SHADING_SOLID = 0,
        COLOR_SHADING_HORIZONTAL,
        COLOR_SHADING_VERTICAL
} ColorShading;

typedef enum {
        BACKGROUND_NONE = 0, /* zero makes this the default placement */
        BACKGROUND_TILED,
        BACKGROUND_CENTERED,
        BACKGROUND_SCALED,
        BACKGROUND_SCALED_ASPECT,
        BACKGROUND_ZOOM
} BackgroundImagePlacement;

struct GdmGreeterBackgroundPrivate
{
        int                      window_width;
        int                      window_height;

        gboolean                 background_enabled;
        char                    *image_filename;
        BackgroundImagePlacement image_placement;
        double                   image_alpha;
        GdkColor                 color1;
        GdkColor                 color2;
        ColorShading             color_shading;

        cairo_surface_t         *surf;
        cairo_pattern_t         *pat;
        int                      pat_width;
        int                      pat_height;
};

enum {
        PROP_0,
};

#define KEY_DIR                "/desktop/gnome/background"
#define KEY_DRAW_BACKGROUND    KEY_DIR "/draw_background"
#define KEY_PRIMARY_COLOR      KEY_DIR "/primary_color"
#define KEY_SECONDARY_COLOR    KEY_DIR "/secondary_color"
#define KEY_COLOR_SHADING_TYPE KEY_DIR "/color_shading_type"
#define KEY_PICTURE_OPTIONS    KEY_DIR "/picture_options"
#define KEY_PICTURE_OPACITY    KEY_DIR "/picture_opacity"
#define KEY_PICTURE_FILENAME   KEY_DIR "/picture_filename"

static void     gdm_greeter_background_class_init  (GdmGreeterBackgroundClass *klass);
static void     gdm_greeter_background_init        (GdmGreeterBackground      *greeter_background);
static void     gdm_greeter_background_finalize    (GObject              *object);

G_DEFINE_TYPE (GdmGreeterBackground, gdm_greeter_background, GTK_TYPE_WINDOW)

static void
gdm_greeter_background_set_property (GObject        *object,
                                     guint           prop_id,
                                     const GValue   *value,
                                     GParamSpec     *pspec)
{
        GdmGreeterBackground *self;

        self = GDM_GREETER_BACKGROUND (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_greeter_background_get_property (GObject        *object,
                                     guint           prop_id,
                                     GValue         *value,
                                     GParamSpec     *pspec)
{
        GdmGreeterBackground *self;

        self = GDM_GREETER_BACKGROUND (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static BackgroundImagePlacement
read_background_image_placement_from_string (const char *string)
{
        BackgroundImagePlacement placement = BACKGROUND_NONE;

        if (string != NULL) {
                if (! strncmp (string, "wallpaper", sizeof ("wallpaper"))) {
                        placement =  BACKGROUND_TILED;
                } else if (! strncmp (string, "centered", sizeof ("centered"))) {
                        placement =  BACKGROUND_CENTERED;
                } else if (! strncmp (string, "scaled", sizeof ("scaled"))) {
                        placement =  BACKGROUND_SCALED_ASPECT;
                } else if (! strncmp (string, "stretched", sizeof ("stretched"))) {
                        placement =  BACKGROUND_SCALED;
                } else if (! strncmp (string, "zoom", sizeof ("zoom"))) {
                        placement =  BACKGROUND_ZOOM;
                }
        }

        return placement;
}

static ColorShading
read_color_shading_from_string (const char *string)
{
        ColorShading shading = COLOR_SHADING_SOLID;

        if (string != NULL) {
                if (! strncmp (string, "vertical-gradient", sizeof ("vertical-gradient"))) {
                        shading = COLOR_SHADING_VERTICAL;
                } else if (! strncmp (string, "horizontal-gradient", sizeof ("horizontal-gradient"))) {
                        shading = COLOR_SHADING_HORIZONTAL;
                }
        }

        return shading;
}

static void
on_key_changed (GConfClient *client,
                guint        cnxn_id,
                GConfEntry  *entry,
                gpointer     data)
{
        const char *key;
        GConfValue *value;

        key = gconf_entry_get_key (entry);

        if (! g_str_has_prefix (key, KEY_DIR)) {
                return;
        }

        value = gconf_entry_get_value (entry);

        /* FIXME: */
        if (strcmp (key, KEY_PICTURE_FILENAME) == 0) {
        }
}

static void
settings_init (GdmGreeterBackground *background)
{
        GConfClient *client;
        GError      *error;
        char        *tmp;
        int          opacity;

        client = gconf_client_get_default ();

        gconf_client_add_dir (client,
                              KEY_DIR,
                              GCONF_CLIENT_PRELOAD_ONELEVEL,
                              NULL);
        gconf_client_notify_add (client,
                                 KEY_DIR,
                                 on_key_changed,
                                 NULL,
                                 NULL,
                                 NULL);

        error = NULL;
        background->priv->background_enabled = gconf_client_get_bool (client, KEY_DRAW_BACKGROUND, &error);
        if (error != NULL) {
                g_warning ("Error reading key: %s", error->message);
                g_error_free (error);
        }

        tmp = gconf_client_get_string (client, KEY_PICTURE_FILENAME, &error);
        if (error != NULL) {
                g_warning ("Error reading key: %s", error->message);
                g_error_free (error);
        }
        if (tmp != NULL) {
                if (g_utf8_validate (tmp, -1, NULL)) {
                        background->priv->image_filename = g_strdup (tmp);
                } else {
                        background->priv->image_filename = g_filename_from_utf8 (tmp, -1, NULL, NULL, NULL);
                }
        }
        g_free (tmp);

        tmp = gconf_client_get_string (client, KEY_PRIMARY_COLOR, &error);
        if (error != NULL) {
                g_warning ("Error reading key: %s", error->message);
                g_error_free (error);
        }
        if (! gdk_color_parse (tmp, &background->priv->color1)) {
                g_warning ("Unable to parse color: %s", tmp);
        }
        g_free (tmp);

        tmp = gconf_client_get_string (client, KEY_SECONDARY_COLOR, &error);
        if (error != NULL) {
                g_warning ("Error reading key: %s", error->message);
                g_error_free (error);
        }
        if (! gdk_color_parse (tmp, &background->priv->color2)) {
                g_warning ("Unable to parse color: %s", tmp);
        }
        g_free (tmp);

        opacity = gconf_client_get_int (client, KEY_PICTURE_OPACITY, &error);
        if (error != NULL) {
                g_warning ("Error reading key: %s", error->message);
                g_error_free (error);
        }
        if (opacity >= 100 || opacity < 0) {
                opacity = 100;
        }
        background->priv->image_alpha = opacity / 100.0;

        tmp = gconf_client_get_string (client, KEY_COLOR_SHADING_TYPE, &error);
        background->priv->color_shading = read_color_shading_from_string (tmp);
        g_free (tmp);

        tmp = gconf_client_get_string (client, KEY_PICTURE_OPTIONS, &error);
        background->priv->image_placement = read_background_image_placement_from_string (tmp);
        g_free (tmp);

        g_object_unref (client);
}

static GdkPixbuf *
scale_pixbuf (GdkPixbuf *pixbuf,
              int        max_width,
              int        max_height,
              gboolean   no_stretch_hint)
{
        int        pw;
        int        ph;
        float      scale_factor_x = 1.0;
        float      scale_factor_y = 1.0;
        float      scale_factor = 1.0;

        g_assert (pixbuf != NULL);
        g_assert (max_width > 0);
        g_assert (max_height > 0);

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        /* Determine which dimension requires the smallest scale. */
        scale_factor_x = (float) max_width / (float) pw;
        scale_factor_y = (float) max_height / (float) ph;

        if (scale_factor_x > scale_factor_y) {
                scale_factor = scale_factor_y;
        } else {
                scale_factor = scale_factor_x;
        }

        /* always scale down, allow to disable scaling up */
        if (scale_factor < 1.0 || !no_stretch_hint) {
                int scale_x = (int) (pw * scale_factor);
                int scale_y = (int) (ph * scale_factor);

                return gdk_pixbuf_scale_simple (pixbuf,
                                                scale_x,
                                                scale_y,
                                                GDK_INTERP_BILINEAR);
        } else {
                return g_object_ref (pixbuf);
        }
}

static void
load_image (GdmGreeterBackground *background)
{
        GdkPixbuf       *pixbuf;
        GdkPixbuf       *scaled;
        cairo_t         *cr;
        int              pw, ph;
        cairo_surface_t *image;

        pixbuf = gdk_pixbuf_new_from_file (background->priv->image_filename, NULL);
        if (pixbuf == NULL) {
                return;
        }

        scaled = scale_pixbuf (pixbuf, background->priv->window_width, background->priv->window_height, TRUE);
        if (scaled != NULL) {
                g_object_unref (pixbuf);
                pixbuf = scaled;
        }

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        if (background->priv->pat != NULL) {
                cairo_pattern_destroy (background->priv->pat);
        }

        image = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            pw,
                                            ph);

        cr = cairo_create (background->priv->surf);

        /* XXX Handle out of memory? */
        gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
        background->priv->pat = cairo_pattern_reference (cairo_get_source (cr));
        background->priv->pat_width = pw;
        background->priv->pat_height = ph;

        cairo_destroy (cr);
        g_object_unref (pixbuf);
}

static void
update_background (GdmGreeterBackground *background)
{
        cairo_t         *cr;

        if (background->priv->window_width <= 0
            || background->priv->window_height <= 0) {
                /* we haven't gotten a configure yet - so don't bother */
                return;
        }

        cr = gdk_cairo_create (GTK_WIDGET (background)->window);

        /* always clear to black */
        cairo_set_source_rgb (cr, 0, 0, 0);
        cairo_rectangle (cr, 0, 0, background->priv->window_width, background->priv->window_height);
        cairo_fill (cr);

        if (background->priv->image_filename != NULL
            && background->priv->image_filename[0] != '\0'
            && background->priv->image_placement != BACKGROUND_NONE) {

                if (background->priv->pat == NULL) {
                        load_image (background);
                }

                if (background->priv->image_placement == BACKGROUND_SCALED) {
                        double sx, sy;

                        sx = (double)background->priv->window_width / background->priv->pat_width;
                        sy = (double)background->priv->window_height / background->priv->pat_height;
                        cairo_scale (cr, sx, sy);
                        g_debug ("scaling w:%d sx:%f sy:%f", background->priv->window_width, sx, sy);

                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);
                } else if (background->priv->image_placement == BACKGROUND_SCALED_ASPECT) {
                        double sx, sy, s;
                        double dx, dy;

                        sx = (double)background->priv->window_width / background->priv->pat_width;
                        sy = (double)background->priv->window_height / background->priv->pat_height;

                        /* scale up long dimension */
                        s = MIN (sx, sy);
                        cairo_scale (cr, s, s);

                        dx = dy = 0;
                        /* now center short dimension */
                        if (sy > sx) {
                                dy = (double)background->priv->window_height - background->priv->pat_height * s;
                        } else {
                                dx = (double)background->priv->window_width - background->priv->pat_width * s;
                        }
                        cairo_translate (cr, dx / 2, dy / 2);
                        g_debug ("aspect scaling w:%d scale:%f dx:%f dy:%f", background->priv->window_width, s, dx, dy);
                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);

                } else if (background->priv->image_placement == BACKGROUND_TILED) {
                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_REPEAT);
                } else {
                        /* centered */
                        double dx, dy;

                        dx = (double)background->priv->window_width - background->priv->pat_width;
                        dy = (double)background->priv->window_height - background->priv->pat_height;
                        cairo_translate (cr, dx / 2, dy / 2);
                        g_debug ("centering dx:%f dy:%f", dx, dy);

                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);
                }

                cairo_set_source (cr, background->priv->pat);
                cairo_paint_with_alpha (cr, background->priv->image_alpha);

        } else if (background->priv->color_shading != COLOR_SHADING_SOLID) {
                cairo_pattern_t *pat;
                g_debug ("color gradient");

                if (background->priv->color_shading == COLOR_SHADING_VERTICAL) {
                        pat = cairo_pattern_create_linear (0.0, 0.0,  0.0, background->priv->window_height);
                } else {
                        pat = cairo_pattern_create_linear (0.0, 0.0,  background->priv->window_width, 0.0);
                }

                cairo_pattern_add_color_stop_rgba (pat,
                                                   0,
                                                   background->priv->color1.red / 65535.0,
                                                   background->priv->color1.green / 65535.0,
                                                   background->priv->color1.blue / 65535.0,
                                                   1.0);
                cairo_pattern_add_color_stop_rgba (pat,
                                                   1,
                                                   background->priv->color2.red / 65535.0,
                                                   background->priv->color2.green / 65535.0,
                                                   background->priv->color2.blue / 65535.0,
                                                   1.0);

                cairo_set_source (cr, pat);

                cairo_rectangle (cr, 0, 0, background->priv->window_width, background->priv->window_height);
                cairo_fill (cr);
                cairo_pattern_destroy (pat);
        } else {
                g_debug ("solid color");
                cairo_set_source_rgba (cr,
                                       background->priv->color1.red / 65535.0,
                                       background->priv->color1.green / 65535.0,
                                       background->priv->color1.blue / 65535.0,
                                       1.0);
                cairo_rectangle (cr, 0, 0, background->priv->window_width, background->priv->window_height);
                cairo_fill (cr);
        }

        cairo_destroy (cr);
}

static GObject *
gdm_greeter_background_constructor (GType                  type,
                                    guint                  n_construct_properties,
                                    GObjectConstructParam *construct_properties)
{
        GdmGreeterBackground      *background;
        GdmGreeterBackgroundClass *klass;

        klass = GDM_GREETER_BACKGROUND_CLASS (g_type_class_peek (GDM_TYPE_GREETER_BACKGROUND));

        background = GDM_GREETER_BACKGROUND (G_OBJECT_CLASS (gdm_greeter_background_parent_class)->constructor (type,
                                                                                                                        n_construct_properties,
                                                                                                                        construct_properties));

        settings_init (background);

        return G_OBJECT (background);
}

static void
gdm_greeter_background_dispose (GObject *object)
{
        GdmGreeterBackground *greeter_background;

        greeter_background = GDM_GREETER_BACKGROUND (object);

        G_OBJECT_CLASS (gdm_greeter_background_parent_class)->dispose (object);
}

static void
gdm_greeter_background_real_map (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->map) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->map (widget);
        }

        /*gdk_window_lower (widget->window);*/
}

static void
on_screen_size_changed (GdkScreen            *screen,
                        GdmGreeterBackground *background)
{
        gtk_widget_queue_resize (GTK_WIDGET (background));
}

static void
gdm_greeter_background_real_realize (GtkWidget *widget)
{
        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->realize (widget);
        }
        gdk_window_set_back_pixmap (widget->window, NULL, FALSE);
        g_signal_connect (gtk_window_get_screen (GTK_WINDOW (widget)),
                          "size_changed",
                          G_CALLBACK (on_screen_size_changed),
                          widget);
}

static void
gdm_greeter_background_real_unrealize (GtkWidget *widget)
{
        g_signal_handlers_disconnect_by_func (gtk_window_get_screen (GTK_WINDOW (widget)),
                                              on_screen_size_changed,
                                              widget);

        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->unrealize) {
                GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->unrealize (widget);
        }
}
static gboolean
gdm_greeter_background_real_configure (GtkWidget         *widget,
                                       GdkEventConfigure *event)
{
        gboolean              handled = FALSE;
        GdmGreeterBackground *background = GDM_GREETER_BACKGROUND (widget);
        cairo_t              *cr;

        g_debug ("configure w: %d h: %d", event->width, event->height);
        background->priv->window_width = event->width;
        background->priv->window_height = event->height;

        if (background->priv->surf != NULL) {
                cairo_surface_destroy (background->priv->surf);
        }

        cr = gdk_cairo_create (widget->window);
        background->priv->surf = cairo_surface_create_similar (cairo_get_target (cr),
                                                               CAIRO_CONTENT_COLOR,
                                                               background->priv->window_width,
                                                               background->priv->window_height);
        cairo_destroy (cr);


        /* schedule a redraw */
        gtk_widget_queue_draw (widget);

        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->configure_event) {
                handled = GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->configure_event (widget, event);
        }

        return handled;
}

static gboolean
gdm_greeter_background_real_expose (GtkWidget      *widget,
                                    GdkEventExpose *event)
{
        gboolean handled = FALSE;

        update_background (GDM_GREETER_BACKGROUND (widget));

        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->expose_event) {
                handled = GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->expose_event (widget, event);
        }

        return handled;
}

static void
gdm_greeter_background_class_init (GdmGreeterBackgroundClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);
        GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

        object_class->get_property = gdm_greeter_background_get_property;
        object_class->set_property = gdm_greeter_background_set_property;
        object_class->constructor = gdm_greeter_background_constructor;
        object_class->dispose = gdm_greeter_background_dispose;
        object_class->finalize = gdm_greeter_background_finalize;

        widget_class->map = gdm_greeter_background_real_map;
        widget_class->realize = gdm_greeter_background_real_realize;
        widget_class->unrealize = gdm_greeter_background_real_unrealize;
        widget_class->expose_event = gdm_greeter_background_real_expose;
        widget_class->configure_event = gdm_greeter_background_real_configure;

        g_type_class_add_private (klass, sizeof (GdmGreeterBackgroundPrivate));
}

static gint
on_delete_event (GdmGreeterBackground *background)
{
        /* Returning true tells GTK+ not to delete the window. */
        return TRUE;
}

static void
gdm_greeter_background_init (GdmGreeterBackground *background)
{
        background->priv = GDM_GREETER_BACKGROUND_GET_PRIVATE (background);

        gtk_window_set_decorated (GTK_WINDOW (background), FALSE);
        gtk_widget_set_app_paintable (GTK_WIDGET (background), TRUE);

        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (background), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (background), TRUE);
        gtk_window_set_type_hint (GTK_WINDOW (background), GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_fullscreen (GTK_WINDOW (background));

        g_signal_connect (background, "delete_event", G_CALLBACK (on_delete_event), NULL);
}

static void
gdm_greeter_background_finalize (GObject *object)
{
        GdmGreeterBackground *greeter_background;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_GREETER_BACKGROUND (object));

        greeter_background = GDM_GREETER_BACKGROUND (object);

        g_return_if_fail (greeter_background->priv != NULL);

        G_OBJECT_CLASS (gdm_greeter_background_parent_class)->finalize (object);
}

GtkWidget *
gdm_greeter_background_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_GREETER_BACKGROUND,
                               NULL);

        return GTK_WIDGET (object);
}
