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

#include <cairo.h>
#if CAIRO_HAS_XLIB_SURFACE
#include <cairo-xlib.h>
#endif

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
        int                      monitor;
        GdkRectangle             geometry;

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

static int
cairo_surface_get_width (cairo_surface_t *surface)
{
        cairo_surface_type_t surf_type;
        int                  w;

        surf_type = cairo_surface_get_type (surface);

        w = 0;

        switch (surf_type) {
#if CAIRO_HAS_XLIB_SURFACE
        case CAIRO_SURFACE_TYPE_XLIB:
                w = cairo_xlib_surface_get_width (surface);
                break;
#endif
        case CAIRO_SURFACE_TYPE_IMAGE:
                w = cairo_image_surface_get_width (surface);
                break;
        default:
                g_warning ("Unknown surface type: %d", surf_type);
                break;
        }

        return w;
}

static int
cairo_surface_get_height (cairo_surface_t *surface)
{
        cairo_surface_type_t surf_type;
        int                  w;

        surf_type = cairo_surface_get_type (surface);

        w = 0;

        g_debug ("Surface type %d", surf_type);
        switch (surf_type) {
#if CAIRO_HAS_XLIB_SURFACE
        case CAIRO_SURFACE_TYPE_XLIB:
                w = cairo_xlib_surface_get_height (surface);
                break;
#endif
        case CAIRO_SURFACE_TYPE_IMAGE:
                w = cairo_image_surface_get_height (surface);
                break;
        default:
                g_warning ("Unknown surface type: %d", surf_type);
                break;
        }

        return w;
}

static void
update_surface (GdmGreeterBackground *background)
{

        g_assert (GTK_WIDGET_REALIZED (GTK_WIDGET (background)));

        if (background->priv->surf == NULL
            || cairo_surface_get_width (background->priv->surf) != background->priv->geometry.width
            || cairo_surface_get_height (background->priv->surf) != background->priv->geometry.height) {
                cairo_t *cr;

                if (background->priv->surf != NULL) {
                        g_debug ("Destroying existing surface w:%d h:%d",
                                 cairo_image_surface_get_width (background->priv->surf),
                                 cairo_image_surface_get_height (background->priv->surf));
                        cairo_surface_destroy (background->priv->surf);
                }

                g_debug ("Creating a new surface for the background w:%d h:%d",
                         background->priv->geometry.width,
                         background->priv->geometry.height);

                cr = gdk_cairo_create (GTK_WIDGET (background)->window);
                background->priv->surf = cairo_surface_create_similar (cairo_get_target (cr),
                                                                       CAIRO_CONTENT_COLOR,
                                                                       background->priv->geometry.width,
                                                                       background->priv->geometry.height);
                g_debug ("Created surface w:%d h:%d",
                         cairo_surface_get_width (background->priv->surf),
                         cairo_surface_get_height (background->priv->surf));
                cairo_destroy (cr);
        }

}

static void
load_image (GdmGreeterBackground *background)
{
        GdkPixbuf       *pixbuf;
        GdkPixbuf       *scaled;
        cairo_t         *cr;
        int              pw, ph;
        GError          *error;

        g_debug ("Loading background from %s", background->priv->image_filename);
        error = NULL;
        pixbuf = gdk_pixbuf_new_from_file (background->priv->image_filename, &error);
        if (pixbuf == NULL) {
                g_warning ("Unable to load image: %s", error->message);
                g_error_free (error);
                return;
        }

        scaled = scale_pixbuf (pixbuf,
                               background->priv->geometry.width,
                               background->priv->geometry.height,
                               TRUE);
        if (scaled != NULL) {
                g_object_unref (pixbuf);
                pixbuf = scaled;
        }

        pw = gdk_pixbuf_get_width (pixbuf);
        ph = gdk_pixbuf_get_height (pixbuf);

        if (background->priv->pat != NULL) {
                cairo_pattern_destroy (background->priv->pat);
        }

        update_surface (background);

        g_assert (background->priv->surf != NULL);

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

        if (background->priv->geometry.width <= 0
            || background->priv->geometry.height <= 0) {
                /* we haven't gotten a configure yet - so don't bother */
                return;
        }

        cr = gdk_cairo_create (GTK_WIDGET (background)->window);

        g_debug ("Clearing background");

        /* always clear to black */
        cairo_set_source_rgb (cr, 0, 0, 0);
        cairo_rectangle (cr, 0, 0, background->priv->geometry.width, background->priv->geometry.height);
        cairo_fill (cr);

        if (background->priv->image_filename != NULL
            && background->priv->image_filename[0] != '\0'
            && background->priv->image_placement != BACKGROUND_NONE) {

                if (background->priv->pat == NULL) {
                        load_image (background);

                        if (background->priv->pat == NULL) {
                                goto out;
                        }
                }

                if (background->priv->image_placement == BACKGROUND_SCALED) {
                        double sx, sy;

                        sx = (double)background->priv->geometry.width / background->priv->pat_width;
                        sy = (double)background->priv->geometry.height / background->priv->pat_height;
                        cairo_scale (cr, sx, sy);
                        if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
                                g_warning ("couldn't scale background: %s", cairo_status_to_string (cairo_status (cr)));
                        }

                        g_debug ("scaling w:%d sx:%f sy:%f", background->priv->geometry.width, sx, sy);

                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);

                } else if (background->priv->image_placement == BACKGROUND_SCALED_ASPECT) {
                        double sx, sy, s;
                        double dx, dy;

                        sx = (double)background->priv->geometry.width / background->priv->pat_width;
                        sy = (double)background->priv->geometry.height / background->priv->pat_height;

                        /* scale up long dimension */
                        s = MIN (sx, sy);
                        cairo_scale (cr, s, s);

                        dx = dy = 0;
                        /* now center short dimension */
                        if (sy > sx) {
                                dy = (double)background->priv->geometry.height - background->priv->pat_height * s;
                        } else {
                                dx = (double)background->priv->geometry.width - background->priv->pat_width * s;
                        }
                        cairo_translate (cr, dx / 2, dy / 2);
                        g_debug ("aspect scaling w:%d scale:%f dx:%f dy:%f", background->priv->geometry.width, s, dx, dy);
                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);

                } else if (background->priv->image_placement == BACKGROUND_TILED) {
                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_REPEAT);
                } else {
                        /* centered */
                        double dx, dy;

                        dx = (double)background->priv->geometry.width - background->priv->pat_width;
                        dy = (double)background->priv->geometry.height - background->priv->pat_height;
                        cairo_translate (cr, dx / 2, dy / 2);
                        g_debug ("centering dx:%f dy:%f", dx, dy);

                        cairo_pattern_set_extend (background->priv->pat, CAIRO_EXTEND_NONE);
                }

                g_debug ("Painting background with alpha %f", background->priv->image_alpha);
                cairo_set_source (cr, background->priv->pat);
                if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
                        g_warning ("couldn't set pattern source: %s", cairo_status_to_string (cairo_status (cr)));
                }
                cairo_paint_with_alpha (cr, background->priv->image_alpha);
                if (cairo_status (cr) != CAIRO_STATUS_SUCCESS) {
                        g_warning ("couldn't paint background: %s", cairo_status_to_string (cairo_status (cr)));
                }

        } else if (background->priv->color_shading != COLOR_SHADING_SOLID) {
                cairo_pattern_t *pat;
                g_debug ("color gradient");

                if (background->priv->color_shading == COLOR_SHADING_VERTICAL) {
                        pat = cairo_pattern_create_linear (0.0, 0.0,  0.0, background->priv->geometry.height);
                } else {
                        pat = cairo_pattern_create_linear (0.0, 0.0,  background->priv->geometry.width, 0.0);
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

                cairo_rectangle (cr, 0, 0, background->priv->geometry.width, background->priv->geometry.height);
                cairo_fill (cr);
                cairo_pattern_destroy (pat);
        } else {
                g_debug ("solid color");
                cairo_set_source_rgba (cr,
                                       background->priv->color1.red / 65535.0,
                                       background->priv->color1.green / 65535.0,
                                       background->priv->color1.blue / 65535.0,
                                       1.0);
                cairo_rectangle (cr, 0, 0, background->priv->geometry.width, background->priv->geometry.height);
                cairo_fill (cr);
        }

out:
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

/* copied from panel-toplevel.c */
static void
gdm_greeter_background_move_resize_window (GdmGreeterBackground *background,
                                      gboolean         move,
                                      gboolean         resize)
{
        GtkWidget *widget;

        widget = GTK_WIDGET (background);

        g_assert (GTK_WIDGET_REALIZED (widget));

        if (move && resize) {
                gdk_window_move_resize (widget->window,
                                        background->priv->geometry.x,
                                        background->priv->geometry.y,
                                        background->priv->geometry.width,
                                        background->priv->geometry.height);
        } else if (move) {
                gdk_window_move (widget->window,
                                 background->priv->geometry.x,
                                 background->priv->geometry.y);
        } else if (resize) {
                gdk_window_resize (widget->window,
                                   background->priv->geometry.width,
                                   background->priv->geometry.height);
        }
}

static GdkRegion *
get_outside_region (GdmGreeterBackground *background)
{
        int        i;
        GdkRegion *region;

        region = gdk_region_new ();
        for (i = 0; i < background->priv->monitor; i++) {
                GdkRectangle geometry;

                gdk_screen_get_monitor_geometry (GTK_WINDOW (background)->screen,
                                                 i,
                                                 &geometry);
                gdk_region_union_with_rect (region, &geometry);
        }

        return region;
}

static void
get_monitor_geometry (GdmGreeterBackground *background,
                      GdkRectangle    *geometry)
{
        GdkRegion   *outside_region;
        GdkRegion   *monitor_region;
        GdkRectangle geom;

        outside_region = get_outside_region (background);

        gdk_screen_get_monitor_geometry (GTK_WINDOW (background)->screen,
                                         background->priv->monitor,
                                         &geom);
        monitor_region = gdk_region_rectangle (&geom);
        gdk_region_subtract (monitor_region, outside_region);
        gdk_region_destroy (outside_region);

        gdk_region_get_clipbox (monitor_region, geometry);
        gdk_region_destroy (monitor_region);
}

static void
update_geometry (GdmGreeterBackground *background,
                 GtkRequisition  *requisition)
{
        GdkRectangle geometry;
        int          height;

        get_monitor_geometry (background, &geometry);

        height = requisition->height;
        background->priv->geometry.width = geometry.width;
        background->priv->geometry.height = geometry.height;

        background->priv->geometry.x = geometry.x;
        background->priv->geometry.y = geometry.y;

        g_debug ("Setting background geometry x:%d y:%d w:%d h:%d",
                 background->priv->geometry.x,
                 background->priv->geometry.y,
                 background->priv->geometry.width,
                 background->priv->geometry.height);
}

static void
gdm_greeter_background_real_size_request (GtkWidget      *widget,
                                          GtkRequisition *requisition)
{
        GdmGreeterBackground *background;
        GtkBin               *bin;
        GdkRectangle          old_geometry;
        int                   position_changed = FALSE;
        int                   size_changed = FALSE;

        background = GDM_GREETER_BACKGROUND (widget);
        bin = GTK_BIN (widget);

        if (bin->child && GTK_WIDGET_VISIBLE (bin->child)) {
                gtk_widget_size_request (bin->child, requisition);
        }

        old_geometry = background->priv->geometry;

        update_geometry (background, requisition);

        requisition->width  = background->priv->geometry.width;
        requisition->height = background->priv->geometry.height;

        if (! GTK_WIDGET_REALIZED (widget)) {
                return;
        }

        if (old_geometry.width  != background->priv->geometry.width ||
            old_geometry.height != background->priv->geometry.height) {
                size_changed = TRUE;
        }

        if (old_geometry.x != background->priv->geometry.x ||
            old_geometry.y != background->priv->geometry.y) {
                position_changed = TRUE;
        }

        gdm_greeter_background_move_resize_window (background, position_changed, size_changed);
}

static gboolean
gdm_greeter_background_real_expose (GtkWidget      *widget,
                                    GdkEventExpose *event)
{
        gboolean handled = FALSE;

        g_debug ("Exposing the background");

        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->expose_event) {
                handled = GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->expose_event (widget, event);
        }

        update_background (GDM_GREETER_BACKGROUND (widget));

        return handled;
}

static gboolean
gdm_greeter_background_real_configure (GtkWidget         *widget,
                                       GdkEventConfigure *event)
{
        gboolean handled;

        handled = FALSE;

        g_debug ("Background configure w: %d h: %d", event->width, event->height);
        if (GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->configure_event) {
                handled = GTK_WIDGET_CLASS (gdm_greeter_background_parent_class)->configure_event (widget, event);
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
        widget_class->size_request = gdm_greeter_background_real_size_request;

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
