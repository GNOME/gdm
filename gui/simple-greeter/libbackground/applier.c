/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* -*- mode: c; style: linux -*- */

/* applier.c
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Written by Bradford Hovinen <hovinen@ximian.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <gnome.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkprivate.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <unistd.h>

#include "applier.h"

#define MONITOR_CONTENTS_X 0 
#define MONITOR_CONTENTS_Y 0
#define MONITOR_CONTENTS_DEFAULT_WIDTH 64
#define MONITOR_CONTENTS_DEFAULT_HEIGHT 48

enum {
	PROP_0,
	PROP_TYPE,
	PROP_PREVIEW_WIDTH,
	PROP_PREVIEW_HEIGHT,
	PROP_SCREEN
};

struct _BGApplierPrivate 
{
	GtkWidget          *preview_widget;        /* The widget for previewing
						    * -- this is not used for
						    * actual rendering; it is
						    * returned if requested */
	BGPreferences      *last_prefs;            /* A cache of the last
						    * bg_preferences structure to
						    * be applied */

	GdkPixbuf          *wallpaper_pixbuf;      /* The "raw" wallpaper pixbuf */

	BGApplierType         type;                  /* Whether we render to the
						    * root or the preview */

	/* Where on the pixmap we should render the background image. Should
         * have origin 0,0 and width and height equal to the desktop size if we
         * are rendering to the desktop. The area to which we render the pixbuf
         * will be smaller if we are rendering a centered image smaller than the
         * screen or scaling and keeping aspect ratio and the background color
         * is solid. */
	GdkRectangle        render_geom;

	/* Where to render the pixbuf, relative to the pixmap. This will be the
	 * same as render_geom above if we have no solid color area to worry
	 * about. By convention, a negative value means that the pixbuf is
	 * larger than the pixmap, so the region should be fetched from a subset
	 * of the pixbuf. */
	GdkRectangle        pixbuf_render_geom;

	/* Where to fetch the data from the pixbuf. We use this in case we are
	 * rendering a centered image that is larger than the size of the
	 * desktop. Otherwise it is (0,0) */
	GdkPoint            pixbuf_xlate;

	/* Geometry of the pixbuf used to render the gradient. If the wallpaper
	 * is not enabled, we use the following optimization: On one dimension,
	 * this should be equal to the dimension of render_geom, while on the
	 * other dimension it should be the constant 32. This avoids wasting
	 * memory with rendundant data. */
	GdkPoint            grad_geom;

	GdkPixbuf          *pixbuf;            /* "working" pixbuf - All data
						* are rendered onto this for
						* display */
	GdkPixmap          *pixmap;            /* Pixmap onto which we dump the
						* pixbuf above when we are ready
						* to render to the screen */
	gboolean            pixmap_is_set;     /* TRUE iff the pixmap above
						* has been set as the root
						* pixmap */
	guint               timeout;           /* "Cleanup" timeout handler;
						* reset to 30 seconds every
						* time apply is called. */
	GdkWindow          *root_window;       /* Root window on which to
						* render the background */
	GdkScreen          *screen;            /* Screen on which to render
						* the background */
	guint               size_changed_cb_id; /* Signal connection id. */
};

static GObjectClass *parent_class;

static void bg_applier_init          (BGApplier           *prefs,
				      BGApplierClass      *class);
static void bg_applier_class_init    (BGApplierClass      *class);
static void bg_applier_base_init     (BGApplierClass      *class);

static void bg_applier_set_prop      (GObject           *object, 
				      guint              prop_id,
				      const GValue      *value,
				      GParamSpec        *pspec);
static void bg_applier_get_prop      (GObject           *object, 
				      guint              prop_id,
				      GValue            *value,
				      GParamSpec        *pspec);

static void bg_applier_dispose       (GObject           *object);
static void bg_applier_finalize      (GObject           *object);

static void run_render_pipeline      (BGApplier           *bg_applier, 
				      const BGPreferences *prefs);
static void draw_disabled_message    (GtkWidget         *widget,
				      const guint width,
				      const guint height);

static void size_changed_cb          (GdkScreen           *screen,
				      BGApplier           *bg_applier);
static void render_background        (BGApplier           *bg_applier,
				      const BGPreferences *prefs);
static void render_wallpaper         (BGApplier           *bg_applier,
				      const BGPreferences *prefs);
static void render_to_screen         (BGApplier           *bg_applier,
				      const BGPreferences *prefs);
static void create_pixmap            (BGApplier           *bg_applier,
				      const BGPreferences *prefs);
static void get_geometry             (wallpaper_type_t   wallpaper_type,
				      GdkPixbuf         *pixbuf,
				      GdkRectangle      *field_geom,
				      GdkRectangle      *virtual_geom,
				      GdkRectangle      *dest_geom,
				      GdkRectangle      *src_geom);

static GdkPixbuf *place_pixbuf       (GdkPixbuf         *dest_pixbuf,
				      GdkPixbuf         *src_pixbuf,
				      GdkRectangle      *dest_geom,
				      GdkRectangle      *src_geom,
				      guint              alpha,
				      GdkColor          *bg_color);
static GdkPixbuf *tile_pixbuf        (GdkPixbuf         *dest_pixbuf,
				      GdkPixbuf         *src_pixbuf,
				      GdkRectangle      *field_geom,
				      guint              alpha,
				      GdkColor          *bg_color);
static void fill_gradient            (GdkPixbuf         *pixbuf,
				      GdkColor          *c1,
				      GdkColor          *c2,
				      orientation_t      orientation);

static gboolean need_wallpaper_load_p  (const BGApplier     *bg_applier,
					const BGPreferences *prefs);
static gboolean need_root_pixmap_p     (const BGApplier     *bg_applier,
					const BGPreferences *prefs);
static gboolean wallpaper_full_cover_p (const BGApplier     *bg_applier,
					const BGPreferences *prefs);
static gboolean render_small_pixmap_p  (const BGPreferences *prefs);

static GdkPixmap *make_root_pixmap   (GdkScreen         *screen,
				      gint               width,
				      gint               height);
static void set_root_pixmap          (GdkPixmap         *pixmap,
				      GdkScreen         *screen);

static gboolean is_nautilus_running  (void);

static gboolean cleanup_cb           (BGApplier *bg_applier);

static void preview_realized_cb      (GtkWidget *preview,
				      BGApplier *bg_applier);

GType
bg_applier_get_type (void)
{
	static GType bg_applier_type = 0;

	if (!bg_applier_type) {
		static GTypeInfo bg_applier_info = {
			sizeof (BGApplierClass),
			(GBaseInitFunc) bg_applier_base_init,
			NULL, /* GBaseFinalizeFunc */
			(GClassInitFunc) bg_applier_class_init,
			NULL, /* GClassFinalizeFunc */
			NULL, /* user-supplied data */
			sizeof (BGApplier),
			0, /* n_preallocs */
			(GInstanceInitFunc) bg_applier_init,
			NULL
		};

		bg_applier_type = 
			g_type_register_static (G_TYPE_OBJECT, 
						"BGApplier",
						&bg_applier_info, 0);
	}

	return bg_applier_type;
}

static void
bg_applier_init (BGApplier *bg_applier, BGApplierClass *class)
{
	bg_applier->p                     = g_new0 (BGApplierPrivate, 1);
	bg_applier->p->last_prefs         = NULL;
	bg_applier->p->pixbuf             = NULL;
	bg_applier->p->wallpaper_pixbuf   = NULL;
	bg_applier->p->timeout            = 0;
	bg_applier->p->render_geom.width  = -1;
	bg_applier->p->render_geom.height = -1;
	bg_applier->p->type               = BG_APPLIER_PREVIEW;

	bg_applier->p->screen            = gdk_screen_get_default ();
	bg_applier->p->root_window       = gdk_screen_get_root_window (bg_applier->p->screen);

	bg_applier->p->size_changed_cb_id = 0;
}

static void
bg_applier_class_init (BGApplierClass *class) 
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);

	object_class->dispose = bg_applier_dispose;
	object_class->finalize = bg_applier_finalize;
	object_class->set_property = bg_applier_set_prop;
	object_class->get_property = bg_applier_get_prop;

	g_object_class_install_property
		(object_class, PROP_TYPE,
		 g_param_spec_int ("type",
				   _("Type"),
				   _("Type of bg_applier: BG_APPLIER_ROOT for root window or BG_APPLIER_PREVIEW for preview"),
				   0, 1, 0,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_PREVIEW_WIDTH,
		 g_param_spec_uint ("preview_width",
			 	    _("Preview Width"),
				    _("Width if applier is a preview: Defaults to 64."),
				    1, 65535, MONITOR_CONTENTS_DEFAULT_WIDTH,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_PREVIEW_HEIGHT,
		 g_param_spec_uint ("preview_height",
				    _("Preview Height"),
				    _("Height if applier is a preview: Defaults to 48."),
				    1, 65535, MONITOR_CONTENTS_DEFAULT_HEIGHT,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property
		(object_class, PROP_SCREEN,
		 g_param_spec_object ("screen",
				      _("Screen"),
				      _("Screen on which BGApplier is to draw"),
				      GDK_TYPE_SCREEN,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	parent_class = 
		G_OBJECT_CLASS (g_type_class_ref (G_TYPE_OBJECT));
}

static void
bg_applier_base_init (BGApplierClass *class) 
{
}

static void
bg_applier_set_prop (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
	BGApplier *bg_applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_BG_APPLIER (object));

	bg_applier = BG_APPLIER (object);

	switch (prop_id) {
	case PROP_TYPE:
		bg_applier->p->type = g_value_get_int (value);

		switch (bg_applier->p->type) {
		case BG_APPLIER_ROOT:
			bg_applier->p->render_geom.x = 0;
			bg_applier->p->render_geom.y = 0;
			bg_applier->p->render_geom.width = gdk_screen_get_width (bg_applier->p->screen);
			bg_applier->p->render_geom.height = gdk_screen_get_height (bg_applier->p->screen);
			bg_applier->p->pixmap = NULL;
			bg_applier->p->pixmap_is_set = FALSE;

			if (bg_applier->p->size_changed_cb_id == 0)
				bg_applier->p->size_changed_cb_id = g_signal_connect (bg_applier->p->screen, "size_changed",
										      G_CALLBACK (size_changed_cb), bg_applier);
			break;

		case BG_APPLIER_PREVIEW:
			if (bg_applier->p->size_changed_cb_id)
				g_signal_handler_disconnect (bg_applier->p->screen,
							     bg_applier->p->size_changed_cb_id);
			bg_applier->p->size_changed_cb_id = 0;
			bg_applier->p->render_geom.x = MONITOR_CONTENTS_X;
			bg_applier->p->render_geom.y = MONITOR_CONTENTS_Y;
			
			if (bg_applier->p->render_geom.width == -1)
			{
				bg_applier->p->render_geom.width = MONITOR_CONTENTS_DEFAULT_WIDTH;
				bg_applier->p->render_geom.height = MONITOR_CONTENTS_DEFAULT_HEIGHT;
			}

			break;

		default:
			g_critical ("Bad bg_applier type: %d", bg_applier->p->type);
			break;
		}

		break;

	case PROP_PREVIEW_WIDTH:
		if (bg_applier->p->type == BG_APPLIER_PREVIEW)
			bg_applier->p->render_geom.width = g_value_get_uint (value);
		break;

	case PROP_PREVIEW_HEIGHT:
		if (bg_applier->p->type == BG_APPLIER_PREVIEW)
			bg_applier->p->render_geom.height = g_value_get_uint (value);
		break;

	case PROP_SCREEN:
		if (bg_applier->p->type == BG_APPLIER_ROOT) {
			if (bg_applier->p->size_changed_cb_id)
				g_signal_handler_disconnect (bg_applier->p->screen,
							     bg_applier->p->size_changed_cb_id);
			bg_applier->p->screen            = g_value_get_object (value);
			bg_applier->p->root_window       = gdk_screen_get_root_window (bg_applier->p->screen);
			bg_applier->p->render_geom.width = gdk_screen_get_width (bg_applier->p->screen);
			bg_applier->p->render_geom.height = gdk_screen_get_height (bg_applier->p->screen);
			bg_applier->p->size_changed_cb_id = g_signal_connect (bg_applier->p->screen, "size_changed",
									      G_CALLBACK (size_changed_cb), bg_applier);
		}
		break;
		
	default:
		g_warning ("Bad property set");
		break;
	}
}

static void
bg_applier_get_prop (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
	BGApplier *bg_applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_BG_APPLIER (object));

	bg_applier = BG_APPLIER (object);

	switch (prop_id) {
	case PROP_TYPE:
		g_value_set_int (value, bg_applier->p->type);
		break;

	case PROP_SCREEN:
		g_value_set_object (value, bg_applier->p->screen);
		break;

	default:
		g_warning ("Bad property get");
		break;
	}
}

static void
bg_applier_dispose (GObject *object) 
{
	BGApplier *bg_applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_BG_APPLIER (object));

	bg_applier = BG_APPLIER (object);

	g_assert (bg_applier->p->pixbuf == NULL);

	if (bg_applier->p->last_prefs != NULL)
		g_object_unref (G_OBJECT (bg_applier->p->last_prefs));
	bg_applier->p->last_prefs = NULL;

	if (bg_applier->p->wallpaper_pixbuf != NULL)
		g_object_unref (G_OBJECT (bg_applier->p->wallpaper_pixbuf));
	bg_applier->p->wallpaper_pixbuf = NULL;

	if (bg_applier->p->size_changed_cb_id)
		g_signal_handler_disconnect (bg_applier->p->screen,
					     bg_applier->p->size_changed_cb_id);
	bg_applier->p->size_changed_cb_id = 0;

	parent_class->dispose (object);
}

static void
bg_applier_finalize (GObject *object) 
{
	BGApplier *bg_applier;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_BG_APPLIER (object));

	bg_applier = BG_APPLIER (object);

	g_free (bg_applier->p);

	parent_class->finalize (object);
}

GObject *
bg_applier_new (BGApplierType type) 
{
	GObject *object;

	object = g_object_new (bg_applier_get_type (),
			       "type", type,
			       NULL);

	return object;
}

GObject *
bg_applier_new_at_size (BGApplierType type,
			const guint width,
			const guint height)
{
	GObject *object;

	object = g_object_new (bg_applier_get_type (),
			       "type", type,
			       "preview_width", width,
			       "preview_height", height,
			       NULL);

	return object;
}

GObject *
bg_applier_new_for_screen (BGApplierType  type,
			   GdkScreen     *screen)
{
	GObject *object;

	g_return_val_if_fail (type == BG_APPLIER_ROOT, NULL);

	object = g_object_new (bg_applier_get_type (),
			       "type", type,
			       "screen", screen,
			       NULL);
	return object;
}

static void
refresh_render (BGApplier *bg_applier,
		BGPreferences *prefs,
		gboolean need_wallpaper_load)
{
	if (bg_applier->p->type == BG_APPLIER_ROOT && is_nautilus_running ()) {
		return;
	}

	if (!prefs->enabled) {
		if (bg_applier->p->type == BG_APPLIER_PREVIEW)
			draw_disabled_message (bg_applier_get_preview_widget (bg_applier), bg_applier->p->render_geom.width, bg_applier->p->render_geom.height);
		return;
	}

	if (need_wallpaper_load) {
		if (bg_applier->p->wallpaper_pixbuf != NULL)
			g_object_unref (G_OBJECT (bg_applier->p->wallpaper_pixbuf));

		bg_applier->p->wallpaper_pixbuf = NULL;

		if (prefs->wallpaper_enabled) {
			g_return_if_fail (prefs->wallpaper_filename != NULL);

			if (prefs->wallpaper_type == WPTYPE_STRETCHED ||
			    prefs->wallpaper_type == WPTYPE_SCALED) {
				bg_applier->p->wallpaper_pixbuf = 
					gdk_pixbuf_new_from_file_at_scale (prefs->wallpaper_filename,
									   bg_applier->p->render_geom.width,
									   bg_applier->p->render_geom.height,
									   prefs->wallpaper_type == WPTYPE_SCALED,
									   NULL);
			} else {
				bg_applier->p->wallpaper_pixbuf = 
					gdk_pixbuf_new_from_file (prefs->wallpaper_filename, NULL);
			}

			if (bg_applier->p->wallpaper_pixbuf == NULL) {
				prefs->wallpaper_enabled = FALSE;
			}
			else if (bg_applier->p->type == BG_APPLIER_ROOT) {
				if (bg_applier->p->timeout)
					g_source_remove (bg_applier->p->timeout);
				bg_applier->p->timeout = g_timeout_add (30000, (GSourceFunc) cleanup_cb, bg_applier);
			}
		}
	}

	run_render_pipeline (bg_applier, prefs);

	if (bg_applier->p->type == BG_APPLIER_PREVIEW && bg_applier->p->preview_widget != NULL)
		gtk_widget_queue_draw (bg_applier->p->preview_widget);
}

static void
size_changed_cb (GdkScreen *screen,
		 BGApplier *bg_applier)
{
	bg_applier->p->render_geom.width = gdk_screen_get_width (bg_applier->p->screen);
	bg_applier->p->render_geom.height = gdk_screen_get_height (bg_applier->p->screen);
	if (bg_applier->p->last_prefs) {
		refresh_render (bg_applier,
				bg_applier->p->last_prefs,
				TRUE);
	}
}

void
bg_applier_apply_prefs (BGApplier           *bg_applier, 
			const BGPreferences *prefs)
{
	BGPreferences *new_prefs;

	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));

	new_prefs = BG_PREFERENCES (bg_preferences_clone (prefs));

	if (new_prefs->wallpaper_type == WPTYPE_NONE)
	{
		new_prefs->wallpaper_enabled = FALSE;
		new_prefs->wallpaper_type = WPTYPE_CENTERED;
	}

	refresh_render (bg_applier, new_prefs, need_wallpaper_load_p (bg_applier, new_prefs));

	if (bg_applier->p->last_prefs != NULL)
		g_object_unref (G_OBJECT (bg_applier->p->last_prefs));

	bg_applier->p->last_prefs = new_prefs;
}

gboolean
bg_applier_render_color_p (const BGApplier *bg_applier, const BGPreferences *prefs) 
{
	g_return_val_if_fail (bg_applier != NULL, FALSE);
	g_return_val_if_fail (IS_BG_APPLIER (bg_applier), FALSE);
	g_return_val_if_fail (prefs != NULL, FALSE);
	g_return_val_if_fail (IS_BG_PREFERENCES (prefs), FALSE);

	return prefs->enabled && !wallpaper_full_cover_p (bg_applier, prefs);
}

GtkWidget *
bg_applier_get_preview_widget (BGApplier *bg_applier) 
{
	if (bg_applier->p->preview_widget == NULL)
	{
		bg_applier->p->preview_widget = gtk_image_new ();

		/* We need to initialize the pixmap, but this
		 * needs GCs, so we have to wait until realize. */
		g_signal_connect (G_OBJECT (bg_applier->p->preview_widget),
				  "realize",
				  (GCallback) preview_realized_cb,
				  bg_applier);
	}

	return bg_applier->p->preview_widget;
}

GdkPixbuf *
bg_applier_get_wallpaper_pixbuf (BGApplier *bg_applier)
{
	g_return_val_if_fail (bg_applier != NULL, NULL);
	g_return_val_if_fail (IS_BG_APPLIER (bg_applier), NULL);

	return bg_applier->p->wallpaper_pixbuf;
}

static void
draw_disabled_message (GtkWidget *widget, const guint w, const guint h)
{
	GdkPixmap      *pixmap;
	GdkColor        color;
	PangoLayout    *layout;
	PangoRectangle  extents;
	GdkGC          *gc;
	gint            x, y;
	const char     *disabled_string = _("Disabled");

	g_return_if_fail (widget != NULL);
	g_return_if_fail (GTK_IS_IMAGE (widget));

	x = MONITOR_CONTENTS_X;
	y = MONITOR_CONTENTS_Y;

	if (!GTK_WIDGET_REALIZED (widget)) {
		gtk_widget_realize (widget);
	}

	gtk_image_get_pixmap (GTK_IMAGE (widget), &pixmap, NULL);
	gc = gdk_gc_new (widget->window);

	color.red = 0x0;
	color.green = 0x0;
	color.blue = 0x0;
	gdk_gc_set_rgb_fg_color (gc, &color);
	gdk_draw_rectangle (pixmap, gc, TRUE, x, y, w, h);

	layout = gtk_widget_create_pango_layout (widget, disabled_string);
	pango_layout_get_pixel_extents (layout, &extents, NULL);

	color.red = 0xffff;
	color.green = 0xffff;
	color.blue = 0xffff;
	gdk_gc_set_rgb_fg_color (gc, &color);

	/* fixme: I do not understand the logic (Lauris) */

	gdk_draw_layout (widget->window,
			 gc,
			 x + (w - extents.width) / 2,
			 y + (h - extents.height) / 2 + extents.height / 2,
			 layout);

	g_object_unref (G_OBJECT (gc));
	g_object_unref (G_OBJECT (layout));
}

static void
run_render_pipeline (BGApplier *bg_applier, const BGPreferences *prefs)
{
	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	g_assert (bg_applier->p->pixbuf == NULL);

	/* Initialize bg_applier->p->render_geom */
	bg_applier->p->pixbuf_render_geom.x = bg_applier->p->render_geom.x;
	bg_applier->p->pixbuf_render_geom.y = bg_applier->p->render_geom.y;
	bg_applier->p->pixbuf_render_geom.width = bg_applier->p->render_geom.width;
	bg_applier->p->pixbuf_render_geom.height = bg_applier->p->render_geom.height;
	bg_applier->p->pixbuf_xlate.x = 0;
	bg_applier->p->pixbuf_xlate.y = 0;

	render_background (bg_applier, prefs);

	if (need_root_pixmap_p (bg_applier, prefs))
		create_pixmap (bg_applier, prefs);

	render_wallpaper (bg_applier, prefs);
	render_to_screen (bg_applier, prefs);

	if (bg_applier->p->pixbuf != NULL) {
		g_object_unref (G_OBJECT (bg_applier->p->pixbuf));
		bg_applier->p->pixbuf = NULL;
	}
}

/* Create the gradient image if necessary and put it into a fresh pixbuf
 *
 * Preconditions:
 *   1. prefs is valid
 *   2. The old bg_applier->p->pixbuf, if it existed, has been destroyed
 *
 * Postconditions (assuming gradient is enabled):
 *   1. bg_applier->p->pixbuf contains a newly rendered gradient
 */

static void
render_background (BGApplier *bg_applier, const BGPreferences *prefs) 
{
	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	if (prefs->gradient_enabled && !wallpaper_full_cover_p (bg_applier, prefs)) {
		bg_applier->p->grad_geom.x = bg_applier->p->render_geom.width;
		bg_applier->p->grad_geom.y = bg_applier->p->render_geom.height;

		if (bg_applier->p->type == BG_APPLIER_ROOT && !prefs->wallpaper_enabled) {
			if (prefs->orientation == ORIENTATION_HORIZ)
				bg_applier->p->grad_geom.y = 32;
			else
				bg_applier->p->grad_geom.x = 32;
		}

		bg_applier->p->pixbuf = 
			gdk_pixbuf_new (GDK_COLORSPACE_RGB, 
					FALSE, 8, 
					bg_applier->p->grad_geom.x, 
					bg_applier->p->grad_geom.y);

		fill_gradient (bg_applier->p->pixbuf,
			       prefs->color1, prefs->color2, 
			       prefs->orientation);

		bg_applier->p->pixbuf_render_geom.width = bg_applier->p->grad_geom.x;
		bg_applier->p->pixbuf_render_geom.height = bg_applier->p->grad_geom.y;
	}
}

/* Render the wallpaper onto the pixbuf-in-progress.
 *
 * Preconditions:
 *   1. The wallpaper pixbuf has been loaded and is in
 *      bg_applier->p->wallpaper_pixbuf.
 *   2. The structure bg_applier->p->render_geom is filled out properly as
 *      described in the documentation above (this should be invariant).
 *   3. The various fields in prefs are valid
 *
 * Postconditions (assuming wallpaper is enabled):
 *   1. bg_applier->p->pixbuf contains the pixbuf-in-progress with the wallpaper
 *      correctly rendered.
 *   2. bg_applier->p->pixbuf_render_geom has been modified, if necessary,
 *      according to the requirements of the wallpaper; it should be set by
 *      default to be the same as bg_applier->p->render_geom.
 */

static void
render_wallpaper (BGApplier *bg_applier, const BGPreferences *prefs) 
{
	GdkRectangle  src_geom = { 0, };
	GdkRectangle  dest_geom = { 0, };
	GdkRectangle  virtual_geom;
	GdkPixbuf    *prescaled_pixbuf = NULL;
	guint         alpha;
	gint          tmp1, tmp2;
	gint          pwidth, pheight;

	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	if (prefs->wallpaper_enabled) {
		if (bg_applier->p->wallpaper_pixbuf == NULL)
			return;

		gdk_drawable_get_size (bg_applier->p->root_window, &tmp1, &tmp2);
		virtual_geom.x = virtual_geom.y = 0;
		virtual_geom.width = tmp1;
		virtual_geom.height = tmp2;

		pwidth = gdk_pixbuf_get_width (bg_applier->p->wallpaper_pixbuf);
		pheight = gdk_pixbuf_get_height (bg_applier->p->wallpaper_pixbuf);

		get_geometry (prefs->wallpaper_type,
			      bg_applier->p->wallpaper_pixbuf,
			      &(bg_applier->p->render_geom),
			      &virtual_geom, &dest_geom, &src_geom);

		/* Modify bg_applier->p->pixbuf_render_geom if necessary */
		if (bg_applier->p->pixbuf == NULL) {   /* This means we didn't render a gradient */
			bg_applier->p->pixbuf_render_geom.x = dest_geom.x + bg_applier->p->render_geom.x;
			bg_applier->p->pixbuf_render_geom.y = dest_geom.y + bg_applier->p->render_geom.y;
			bg_applier->p->pixbuf_render_geom.width = dest_geom.width;
			bg_applier->p->pixbuf_render_geom.height = dest_geom.height;
		}

		if (prefs->wallpaper_type == WPTYPE_TILED) {
			if (dest_geom.width != pwidth || dest_geom.height != pheight) {
				int hscale = pwidth * bg_applier->p->render_geom.width / virtual_geom.width;
				int vscale = pheight * bg_applier->p->render_geom.height / virtual_geom.height;

				if (hscale < 1) hscale = 1;
				if (vscale < 1) vscale = 1;

				prescaled_pixbuf = gdk_pixbuf_scale_simple
					(bg_applier->p->wallpaper_pixbuf, hscale, vscale,
					 GDK_INTERP_BILINEAR);
			} else {
				prescaled_pixbuf = bg_applier->p->wallpaper_pixbuf;
				g_object_ref (G_OBJECT (prescaled_pixbuf));
			}
		}

		if (prefs->adjust_opacity) {
			alpha = 2.56 * prefs->opacity;
			alpha = alpha * alpha / 256;
			alpha = CLAMP (alpha, 0, 255);
		} else {
			alpha = 255;
		}

		if (prefs->wallpaper_type == WPTYPE_TILED)
			bg_applier->p->pixbuf = tile_pixbuf (bg_applier->p->pixbuf,
							  prescaled_pixbuf,
							  &(bg_applier->p->render_geom),
							  alpha, prefs->color1);
		else
			bg_applier->p->pixbuf = place_pixbuf (bg_applier->p->pixbuf,
							   bg_applier->p->wallpaper_pixbuf,
							   &dest_geom, &src_geom,
							   alpha, prefs->color1);

		if (bg_applier->p->pixbuf == bg_applier->p->wallpaper_pixbuf) {
			bg_applier->p->pixbuf_xlate.x = src_geom.x;
			bg_applier->p->pixbuf_xlate.y = src_geom.y;
		}

		if (prescaled_pixbuf != NULL)
			g_object_unref (G_OBJECT (prescaled_pixbuf));
	}
}

/* Take whatever we have rendered and transfer it to the display.
 *
 * Preconditions:
 *   1. We have already rendered the gradient and wallpaper, and
 *      bg_applier->p->pixbuf is a valid GdkPixbuf containing that rendered data.
 *   2. The structure bg_applier->p->pixbuf_render_geom contains the coordonites on
 *      the destination visual to which we should render the contents of
 *      bg_applier->p->pixbuf
 *   3. The structure bg_applier->p->render_geom contains the total area that the
 *      background should cover (i.e. the whole desktop if we are rendering to
 *      the root window, or the region inside the monitor if we are rendering to
 *      the preview).
 *   4. The strucutre prefs->color1 contains the background color to be used on
 *      areas not covered by the above pixbuf.
 */

static void
render_to_screen (BGApplier *bg_applier, const BGPreferences *prefs) 
{
	GdkGC *gc;

	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	gc = gdk_gc_new (bg_applier->p->pixmap);

	if (bg_applier->p->pixbuf != NULL) {
		if (bg_applier->p->pixbuf_render_geom.x != 0 ||
		    bg_applier->p->pixbuf_render_geom.y != 0 ||
		    bg_applier->p->pixbuf_render_geom.width != bg_applier->p->render_geom.width ||
		    bg_applier->p->pixbuf_render_geom.height != bg_applier->p->render_geom.height)
		{
			gboolean success;

#if 0
			gdk_color_alloc (gdk_window_get_colormap (bg_applier->p->root_window), prefs->color1);
#else
			gdk_colormap_alloc_colors (gdk_drawable_get_colormap (bg_applier->p->root_window),
						   prefs->color1, 1, FALSE, TRUE, &success);
#endif

			gdk_gc_set_foreground (gc, prefs->color1);
			gdk_draw_rectangle (bg_applier->p->pixmap, gc, TRUE,
					    bg_applier->p->render_geom.x,
					    bg_applier->p->render_geom.y, 
					    bg_applier->p->render_geom.width,
					    bg_applier->p->render_geom.height);
		}

		gdk_pixbuf_render_to_drawable
			(bg_applier->p->pixbuf,
			 bg_applier->p->pixmap, gc,
			 bg_applier->p->pixbuf_xlate.x,
			 bg_applier->p->pixbuf_xlate.y,
			 bg_applier->p->pixbuf_render_geom.x,
			 bg_applier->p->pixbuf_render_geom.y,
			 bg_applier->p->pixbuf_render_geom.width,
			 bg_applier->p->pixbuf_render_geom.height,
			 GDK_RGB_DITHER_MAX, 0, 0);
	} else {
		if (bg_applier->p->type == BG_APPLIER_ROOT) {
			gboolean success;

#if 0
			gdk_color_alloc (gdk_window_get_colormap (bg_applier->p->root_window), prefs->color1);
#else
			gdk_colormap_alloc_colors (gdk_drawable_get_colormap (bg_applier->p->root_window),
						   prefs->color1, 1, FALSE, TRUE, &success);
#endif
			gdk_window_set_background (bg_applier->p->root_window, prefs->color1);
			gdk_window_clear (bg_applier->p->root_window);
		}
		else if (bg_applier->p->type == BG_APPLIER_PREVIEW) {
			gboolean success;

#if 0
			gdk_color_alloc (gdk_window_get_colormap (bg_applier->p->preview_widget->window), prefs->color1);
#else
			gdk_colormap_alloc_colors (gdk_drawable_get_colormap (bg_applier->p->root_window),
						   prefs->color1, 1, FALSE, TRUE, &success);
#endif

			if (bg_applier->p->type == BG_APPLIER_PREVIEW) {
				gdk_gc_set_foreground (gc, prefs->color1);
				gdk_draw_rectangle (bg_applier->p->pixmap, gc, TRUE,
						    bg_applier->p->render_geom.x,
						    bg_applier->p->render_geom.y, 
						    bg_applier->p->render_geom.width,
						    bg_applier->p->render_geom.height);
			}
			else if (bg_applier->p->type == BG_APPLIER_ROOT) {
				gdk_window_set_back_pixmap (bg_applier->p->root_window, NULL, FALSE);
				gdk_window_set_background (bg_applier->p->root_window, prefs->color1);
			}
		}
	}

	if (bg_applier->p->type == BG_APPLIER_ROOT && !bg_applier->p->pixmap_is_set &&
	    (prefs->wallpaper_enabled || prefs->gradient_enabled))
		set_root_pixmap (bg_applier->p->pixmap, bg_applier->p->screen);
	else if (bg_applier->p->type == BG_APPLIER_ROOT && !bg_applier->p->pixmap_is_set)
		set_root_pixmap (NULL, bg_applier->p->screen);

	g_object_unref (G_OBJECT (gc));
}

/* Create a pixmap that will replace the current root pixmap. This function has
 * no effect if the bg_applier is for the preview window
 */

static void
create_pixmap (BGApplier *bg_applier, const BGPreferences *prefs) 
{
	gint width, height;

	g_return_if_fail (bg_applier != NULL);
	g_return_if_fail (IS_BG_APPLIER (bg_applier));
	g_return_if_fail (prefs != NULL);
	g_return_if_fail (IS_BG_PREFERENCES (prefs));

	switch (bg_applier->p->type) {
	case BG_APPLIER_ROOT:
		if (prefs->gradient_enabled && !prefs->wallpaper_enabled) {
			width = bg_applier->p->grad_geom.x;
			height = bg_applier->p->grad_geom.y;
		} else {
			width = bg_applier->p->render_geom.width;
			height = bg_applier->p->render_geom.height;
		}

		bg_applier->p->pixmap = make_root_pixmap (bg_applier->p->screen, width, height);
		bg_applier->p->pixmap_is_set = FALSE;
		break;

	case BG_APPLIER_PREVIEW:
		bg_applier_get_preview_widget (bg_applier);

		if (!GTK_WIDGET_REALIZED (bg_applier->p->preview_widget))
			gtk_widget_realize (bg_applier->p->preview_widget);

#if 0
		bg_applier->p->pixmap = GTK_PIXMAP (bg_applier->p->preview_widget)->pixmap;
#else
		if (!bg_applier->p->pixmap)
			gtk_image_get_pixmap (GTK_IMAGE (bg_applier->p->preview_widget), &bg_applier->p->pixmap, NULL);
#endif
		bg_applier->p->pixmap_is_set = TRUE;
		break;
	}
}

/* Compute geometry information based on the wallpaper type. In particular,
 * determine where on the destination visual the wallpaper should be rendered
 * and where the data from the source pixbuf the image should be fetched.
 */

static void
get_geometry (wallpaper_type_t  wallpaper_type,
	      GdkPixbuf        *pixbuf,
	      GdkRectangle     *field_geom,
	      GdkRectangle     *virtual_geom,
	      GdkRectangle     *dest_geom,
	      GdkRectangle     *src_geom)
{
	gdouble asp, xfactor, yfactor;
	gint pwidth, pheight;
	gint st = 0;

	xfactor = (gdouble) field_geom->width / (gdouble) virtual_geom->width;
	yfactor = (gdouble) field_geom->height / (gdouble) virtual_geom->height;

	pwidth = gdk_pixbuf_get_width (pixbuf);
	pheight = gdk_pixbuf_get_height (pixbuf);

	switch (wallpaper_type) {
	case WPTYPE_TILED:
		src_geom->x = src_geom->y = 0;
		dest_geom->x = dest_geom->y = 0;

		src_geom->width = pwidth;
		src_geom->height = pheight;

		dest_geom->width = field_geom->width;
		dest_geom->height = field_geom->height;

		break;

	case WPTYPE_CENTERED:
		if (virtual_geom->width < pwidth) {
			src_geom->width = virtual_geom->width;
			src_geom->x = (pwidth - virtual_geom->width) / 2;
			dest_geom->width = field_geom->width;
			dest_geom->x = 0;
		} else {
			src_geom->width = pwidth;
			src_geom->x = 0;
			dest_geom->width = MIN ((gdouble) src_geom->width * xfactor, field_geom->width);
			dest_geom->x = (field_geom->width - dest_geom->width) / 2;
		}

		if (virtual_geom->height < pheight) {
			src_geom->height = virtual_geom->height;
			src_geom->y = (pheight - virtual_geom->height) / 2;
			dest_geom->height = field_geom->height;
			dest_geom->y = 0;
		} else {
			src_geom->height = pheight;
			src_geom->y = 0;
			dest_geom->height = MIN ((gdouble) src_geom->height * yfactor, field_geom->height);
			dest_geom->y = (field_geom->height - dest_geom->height) / 2;
		}

		break;

	case WPTYPE_SCALED:
		asp = (gdouble) pwidth / (gdouble) virtual_geom->width;

		if (asp < (gdouble) pheight / virtual_geom->height) {
			asp = (gdouble) pheight / (gdouble) virtual_geom->height;
			st = 1;
		}

		if (st) {
			dest_geom->width = pwidth / asp * xfactor;
			dest_geom->height = field_geom->height;
			dest_geom->x = (field_geom->width - dest_geom->width) / 2;
			dest_geom->y = 0;
		} else {
			dest_geom->height = pheight / asp * yfactor;
			dest_geom->width = field_geom->width;
			dest_geom->x = 0;
			dest_geom->y = (field_geom->height - dest_geom->height) / 2;
		}

		src_geom->x = src_geom->y = 0;
		src_geom->width = pwidth;
		src_geom->height = pheight;

		break;

	case WPTYPE_ZOOM:
		asp = (gdouble) pwidth / (gdouble) virtual_geom->width;

		if (asp > (gdouble) pheight / virtual_geom->height) {
			src_geom->width = pheight * virtual_geom->width / virtual_geom->height;
			src_geom->height = pheight;
			src_geom->x = (pwidth - src_geom->width) / 2;
			src_geom->y = 0;
		} else {
			src_geom->width = pwidth;
			src_geom->height = pwidth * virtual_geom->height / virtual_geom->width;
			src_geom->x = 0;
			src_geom->y = (pheight - src_geom->height) / 2;
		}

		dest_geom->x = dest_geom->y = 0;
		dest_geom->width = field_geom->width;
		dest_geom->height = field_geom->height;

		break;

	case WPTYPE_STRETCHED:
		dest_geom->width = field_geom->width;
		dest_geom->height = field_geom->height;
		dest_geom->x = 0;
		dest_geom->y = 0;
		src_geom->x = src_geom->y = 0;
		src_geom->width = pwidth;
		src_geom->height = pheight;
		break;
	default:
		g_error ("Bad wallpaper type");
		break;
	}
}

/* Place one pixbuf onto another, compositing and scaling as necessary */

static GdkPixbuf *
place_pixbuf (GdkPixbuf *dest_pixbuf,
	      GdkPixbuf *src_pixbuf,
	      GdkRectangle *dest_geom,
	      GdkRectangle *src_geom,
	      guint alpha,
	      GdkColor *bg_color) 
{
	gboolean need_composite;
	gboolean need_scaling;
	gdouble scale_x, scale_y;
	gint real_dest_x, real_dest_y;
	guint colorv;

	need_composite = (alpha < 255 || gdk_pixbuf_get_has_alpha (src_pixbuf));
	need_scaling = ((dest_geom->width != src_geom->width) || (dest_geom->height != src_geom->height));

	if (need_scaling) {
		scale_x = (gdouble) dest_geom->width / (gdouble) src_geom->width;
		scale_y = (gdouble) dest_geom->height / (gdouble) src_geom->height;
	} else {
		scale_x = scale_y = 1.0;
	}

	if (need_composite && dest_pixbuf != NULL) {
		gdk_pixbuf_composite
			(src_pixbuf, dest_pixbuf,
			 dest_geom->x, dest_geom->y,
			 dest_geom->width, 
			 dest_geom->height,
			 dest_geom->x - src_geom->x * scale_x,
			 dest_geom->y - src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR,
			 alpha);
	}
	else if (need_composite) {
		dest_pixbuf = gdk_pixbuf_new
			(GDK_COLORSPACE_RGB, FALSE, 8,
			 dest_geom->width, dest_geom->height);

		colorv = ((bg_color->red & 0xff00) << 8) |
			(bg_color->green & 0xff00) |
			((bg_color->blue & 0xff00) >> 8);

		gdk_pixbuf_composite_color 
			(src_pixbuf, dest_pixbuf,
			 0, 0,
			 dest_geom->width, 
			 dest_geom->height,
			 -src_geom->x * scale_x,
			 -src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR,
			 alpha, 0, 0, 65536,
			 colorv, colorv);
	}
	else if (need_scaling) {
		if (dest_pixbuf == NULL) {
			dest_pixbuf = gdk_pixbuf_new
				(GDK_COLORSPACE_RGB, FALSE, 8,
				 dest_geom->width, dest_geom->height);
			real_dest_x = real_dest_y = 0;
		} else {
			real_dest_x = dest_geom->x;
			real_dest_y = dest_geom->y;
		}

		gdk_pixbuf_scale 
			(src_pixbuf, dest_pixbuf,
			 real_dest_x, real_dest_y,
			 dest_geom->width,
			 dest_geom->height,
			 real_dest_x - src_geom->x * scale_x,
			 real_dest_y - src_geom->y * scale_y,
			 scale_x, scale_y,
			 GDK_INTERP_BILINEAR);
	}
	else if (dest_pixbuf != NULL) {
		gdk_pixbuf_copy_area
			(src_pixbuf,
			 src_geom->x, src_geom->y, 
			 src_geom->width,
			 src_geom->height,
			 dest_pixbuf,
			 dest_geom->x, dest_geom->y);
	} else {
		dest_pixbuf = src_pixbuf;
		g_object_ref (G_OBJECT (dest_pixbuf));
	}

	return dest_pixbuf;
}

/* Tile one pixbuf repeatedly onto another, compositing as necessary. Assumes
 * that the source pixbuf has already been scaled properly
 */

static GdkPixbuf *
tile_pixbuf (GdkPixbuf *dest_pixbuf,
	     GdkPixbuf *src_pixbuf,
	     GdkRectangle *field_geom,
	     guint alpha,
	     GdkColor *bg_color) 
{
	gboolean need_composite;
	gboolean use_simple;
	gdouble cx, cy;
	gdouble colorv;
	gint pwidth, pheight;

	need_composite = (alpha < 255 || gdk_pixbuf_get_has_alpha (src_pixbuf));
	use_simple = (dest_pixbuf == NULL);

	if (dest_pixbuf == NULL)
		dest_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, field_geom->width, field_geom->height);

	if (need_composite && use_simple)
		colorv = ((bg_color->red & 0xff00) << 8) |
			(bg_color->green & 0xff00) |
			((bg_color->blue & 0xff00) >> 8);
	else
		colorv = 0;

	pwidth = gdk_pixbuf_get_width (src_pixbuf);
	pheight = gdk_pixbuf_get_height (src_pixbuf);

	for (cy = 0; cy < field_geom->height; cy += pheight) {
		for (cx = 0; cx < field_geom->width; cx += pwidth) {
			if (need_composite && !use_simple)
				gdk_pixbuf_composite
					(src_pixbuf, dest_pixbuf,
					 cx, cy,
					 MIN (pwidth, field_geom->width - cx), 
					 MIN (pheight, field_geom->height - cy),
					 cx, cy,
					 1.0, 1.0,
					 GDK_INTERP_BILINEAR,
					 alpha);
			else if (need_composite && use_simple)
				gdk_pixbuf_composite_color
					(src_pixbuf, dest_pixbuf,
					 cx, cy,
					 MIN (pwidth, field_geom->width - cx), 
					 MIN (pheight, field_geom->height - cy),
					 cx, cy,
					 1.0, 1.0,
					 GDK_INTERP_BILINEAR,
					 alpha,
					 65536, 65536, 65536,
					 colorv, colorv);
			else
				gdk_pixbuf_copy_area
					(src_pixbuf,
					 0, 0,
					 MIN (pwidth, field_geom->width - cx),
					 MIN (pheight, field_geom->height - cy),
					 dest_pixbuf,
					 cx, cy);
		}
	}

	return dest_pixbuf;
}

/* Fill a raw character array with gradient data; the data may then be imported
 * into a GdkPixbuf
 */

static void
fill_gradient (GdkPixbuf     *pixbuf,
	       GdkColor      *c1,
	       GdkColor      *c2,
	       orientation_t  orientation)
{
	int i, j;
	int dr, dg, db;
	int gs1;
	int vc = ((orientation == ORIENTATION_HORIZ) || (c1 == c2));
	int w = gdk_pixbuf_get_width (pixbuf);
	int h = gdk_pixbuf_get_height (pixbuf);
	guchar *b, *row;
	guchar *d = gdk_pixbuf_get_pixels (pixbuf);
	int rowstride = gdk_pixbuf_get_rowstride (pixbuf);

#define R1 c1->red
#define G1 c1->green
#define B1 c1->blue
#define R2 c2->red
#define G2 c2->green
#define B2 c2->blue

	dr = R2 - R1;
	dg = G2 - G1;
	db = B2 - B1;

	gs1 = (orientation == ORIENTATION_VERT) ? h-1 : w-1;

	row = g_new (unsigned char, rowstride);

	if (vc) {
		b = row;
		for (j = 0; j < w; j++) {
			*b++ = (R1 + (j * dr) / gs1) >> 8;
			*b++ = (G1 + (j * dg) / gs1) >> 8;
			*b++ = (B1 + (j * db) / gs1) >> 8;
		}
	}

	for (i = 0; i < h; i++) {
		if (!vc) {
			unsigned char cr, cg, cb;
			cr = (R1 + (i * dr) / gs1) >> 8;
			cg = (G1 + (i * dg) / gs1) >> 8;
			cb = (B1 + (i * db) / gs1) >> 8;
			b = row;
			for (j = 0; j < w; j++) {
				*b++ = cr;
				*b++ = cg;
				*b++ = cb;
			}
		}
		memcpy (d, row, w * 3);
		d += rowstride;
	}

#undef R1
#undef G1
#undef B1
#undef R2
#undef G2
#undef B2

	g_free (row);
}

/* Boolean predicates to assist optimization and rendering */

/* Return TRUE iff the wallpaper filename or enabled settings have changed
 * between old_prefs and new_prefs
 */

static gboolean
need_wallpaper_load_p (const BGApplier *bg_applier, const BGPreferences *prefs)
{
	if (bg_applier->p->last_prefs == NULL)
		return TRUE;
	else if (prefs->wallpaper_enabled && bg_applier->p->wallpaper_pixbuf == NULL)
		return TRUE;
	else if (bg_applier->p->last_prefs->wallpaper_enabled != prefs->wallpaper_enabled)
		return TRUE;
	else if (!bg_applier->p->last_prefs->wallpaper_enabled && !prefs->wallpaper_enabled)
		return FALSE;
	else if (strcmp (bg_applier->p->last_prefs->wallpaper_filename, prefs->wallpaper_filename))
		return TRUE;
	else if (bg_applier->p->last_prefs->wallpaper_type == prefs->wallpaper_type)
		return FALSE;
	else if (bg_applier->p->last_prefs->wallpaper_type != WPTYPE_TILED &&
		 bg_applier->p->last_prefs->wallpaper_type != WPTYPE_CENTERED)
		return TRUE;
	else if (prefs->wallpaper_type != WPTYPE_TILED &&
		 prefs->wallpaper_type != WPTYPE_CENTERED)
		return TRUE;
	else
		return FALSE;
}

/* Return TRUE iff we need to create a new root pixmap */

static gboolean
need_root_pixmap_p (const BGApplier *bg_applier, const BGPreferences *prefs) 
{
	if (bg_applier->p->pixmap == NULL)
		return TRUE;
	else if (prefs->wallpaper_enabled == FALSE && prefs->gradient_enabled == FALSE)
		return FALSE;
	else if (bg_applier->p->last_prefs == NULL)
		return TRUE;
	else if (bg_applier->p->last_prefs->wallpaper_enabled == FALSE && bg_applier->p->last_prefs->gradient_enabled == FALSE)
		return TRUE;
	else if (render_small_pixmap_p (bg_applier->p->last_prefs) != render_small_pixmap_p (prefs))
		return TRUE;
	else if (!render_small_pixmap_p (bg_applier->p->last_prefs) &&
		 !render_small_pixmap_p (prefs))
		return FALSE;
	else if (bg_applier->p->last_prefs->orientation != prefs->orientation)
		return TRUE;
	else
		return FALSE;
}

/* Return TRUE iff the colors are equal */

/* Return TRUE iff the wallpaper completely covers the colors in the given
 * bg_preferences structure, assuming we have already loaded the wallpaper pixbuf */

static gboolean
wallpaper_full_cover_p (const BGApplier *bg_applier, const BGPreferences *prefs) 
{
	gint swidth, sheight;
	gint pwidth, pheight;
	gdouble asp1, asp2;

	if (bg_applier->p->wallpaper_pixbuf == NULL)
		return FALSE;
	else if (gdk_pixbuf_get_has_alpha (bg_applier->p->wallpaper_pixbuf))
		return FALSE;
	else if (prefs->wallpaper_type == WPTYPE_TILED)
		return TRUE;
	else if (prefs->wallpaper_type == WPTYPE_STRETCHED)
		return TRUE;

	gdk_drawable_get_size (bg_applier->p->root_window, &swidth, &sheight);
	pwidth = gdk_pixbuf_get_width (bg_applier->p->wallpaper_pixbuf);
	pheight = gdk_pixbuf_get_height (bg_applier->p->wallpaper_pixbuf);

	if (prefs->wallpaper_type == WPTYPE_CENTERED) {
		if (pwidth >= swidth && pheight >= sheight)
			return TRUE;
		else
			return FALSE;
	}
	else if (prefs->wallpaper_type == WPTYPE_SCALED) {
		asp1 = (gdouble) swidth / (gdouble) sheight;
		asp2 = (gdouble) pwidth / (gdouble) pheight;

		if (swidth * (asp1 - asp2) < 1 && swidth * (asp2 - asp1) < 1)
			return TRUE;
		else
			return FALSE;
	}

	return FALSE;
}

/* Return TRUE if we can optimize the rendering by using a small thin pixmap */

static gboolean
render_small_pixmap_p (const BGPreferences *prefs) 
{
	return prefs->gradient_enabled && !prefs->wallpaper_enabled;
}

/* Create a persistent pixmap. We create a separate display
 * and set the closedown mode on it to RetainPermanent
 */
static GdkPixmap *
make_root_pixmap (GdkScreen *screen, gint width, gint height)
{
	Display *display;
	char *display_name;
	Pixmap result;
	GdkPixmap *gdk_pixmap;
	int screen_num;

	screen_num = gdk_screen_get_number (screen);

	gdk_flush ();

	display_name = DisplayString (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));

	display = XOpenDisplay (display_name);

        if (display == NULL) {
                g_warning ("Unable to open display '%s' when setting background pixmap\n",
                           (display_name) ? display_name : "NULL");
                return NULL;
        }

	XSetCloseDownMode (display, RetainPermanent);

	result = XCreatePixmap (display,
				RootWindow (display, screen_num),
				width, height,
				DefaultDepth (display, screen_num));

	XCloseDisplay (display);

	gdk_pixmap = gdk_pixmap_foreign_new (result);
	gdk_drawable_set_colormap (GDK_DRAWABLE (gdk_pixmap),
				   gdk_drawable_get_colormap (gdk_screen_get_root_window (screen)));

	return gdk_pixmap;
}

/* Set the root pixmap, and properties pointing to it. We
 * do this atomically with XGrabServer to make sure that
 * we won't leak the pixmap if somebody else it setting
 * it at the same time. (This assumes that they follow the
 * same conventions we do)
 */

static void 
set_root_pixmap (GdkPixmap *pixmap, GdkScreen *screen) 
{
	Atom type;
	gulong nitems, bytes_after;
	gint format;
	guchar *data_esetroot;
	Pixmap pixmap_id;
	Display *display;
	int screen_num;

	/* Final check to see if nautilus is running. If it is, we don't
	   touch the root pixmap at all. */
	if (is_nautilus_running ())
		return;

	screen_num = gdk_screen_get_number (screen);

	if (pixmap != NULL)
		pixmap_id = GDK_WINDOW_XWINDOW (pixmap);
	else
		pixmap_id = 0;

	display = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	XGrabServer (display);

	XGetWindowProperty (display, RootWindow (display, screen_num),
			    XInternAtom (display, "ESETROOT_PMAP_ID", False),
			    0L, 1L, False, XA_PIXMAP,
			    &type, &format, &nitems, &bytes_after,
			    &data_esetroot);

	if (type == XA_PIXMAP) {
		if (format == 32 && nitems == 1) {
			Pixmap old_pixmap;

			old_pixmap = *((Pixmap *) data_esetroot);

			if (pixmap != NULL && old_pixmap != pixmap_id)
				XKillClient (display, old_pixmap);
			else if (pixmap == NULL)
				pixmap_id = old_pixmap;
		}

		XFree (data_esetroot);
	}

	if (pixmap != NULL) {
		XChangeProperty (display, RootWindow (display, screen_num),
				 XInternAtom (display, "ESETROOT_PMAP_ID", FALSE),
				 XA_PIXMAP, 32, PropModeReplace,
				 (guchar *) &pixmap_id, 1);
		XChangeProperty (display, RootWindow (display, screen_num),
				 XInternAtom (display, "_XROOTPMAP_ID", FALSE),
				 XA_PIXMAP, 32, PropModeReplace,
				 (guchar *) &pixmap_id, 1);

		XSetWindowBackgroundPixmap (display, RootWindow (display, screen_num),
					    pixmap_id);
	} else if (pixmap == NULL) {
		XDeleteProperty (display, RootWindow (display, screen_num),
				 XInternAtom (display, "ESETROOT_PMAP_ID", FALSE));
		XDeleteProperty (display, RootWindow (display, screen_num),
				 XInternAtom (display, "_XROOTPMAP_ID", FALSE));
	}

	XClearWindow (display, RootWindow (display, screen_num));
	XUngrabServer (display);
	XFlush (display);
}

static gboolean
is_nautilus_running (void)
{
	Atom window_id_atom;
	Window nautilus_xid;
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *data;
	int retval;
	Atom wmclass_atom;
	gboolean running;
	gint error;

	window_id_atom = XInternAtom (GDK_DISPLAY (), 
				      "NAUTILUS_DESKTOP_WINDOW_ID", True);

	if (window_id_atom == None) return FALSE;

	retval = XGetWindowProperty (GDK_DISPLAY (), GDK_ROOT_WINDOW (),
				     window_id_atom, 0, 1, False, XA_WINDOW,
				     &actual_type, &actual_format, &nitems,
				     &bytes_after, &data);

	if (data != NULL) {
		nautilus_xid = *(Window *) data;
		XFree (data);
	} else {
		return FALSE;
	}

	if (actual_type != XA_WINDOW) return FALSE;
	if (actual_format != 32) return FALSE;

	wmclass_atom = XInternAtom (GDK_DISPLAY (), "WM_CLASS", False);

	gdk_error_trap_push ();

	retval = XGetWindowProperty (GDK_DISPLAY (), nautilus_xid,
				     wmclass_atom, 0, 24, False, XA_STRING,
				     &actual_type, &actual_format, &nitems,
				     &bytes_after, &data);

	error = gdk_error_trap_pop ();

	if (error == BadWindow) return FALSE;

	if (actual_type == XA_STRING &&
	    nitems == 24 &&
	    bytes_after == 0 &&
	    actual_format == 8 &&
	    data != NULL &&
	    !strcmp ((char *)data, "desktop_window") &&
	    !strcmp ((char *)data + strlen ((char *)data) + 1, "Nautilus"))
		running = TRUE;
	else
		running = FALSE;

	if (data != NULL)
		XFree (data);

	return running;
}

static gboolean
cleanup_cb (BGApplier *bg_applier)
{
	g_message ("cleanup_cb: Enter");

	if (bg_applier->p->wallpaper_pixbuf != NULL) {
		g_object_unref (G_OBJECT (bg_applier->p->wallpaper_pixbuf));
		bg_applier->p->wallpaper_pixbuf = NULL;	
	}

	if (bg_applier->p->pixbuf != NULL) {
		g_object_unref (G_OBJECT (bg_applier->p->pixbuf));
		bg_applier->p->pixbuf = NULL;
	}

	bg_applier->p->timeout = 0;
	
	return FALSE;
}

static void
preview_realized_cb (GtkWidget *preview, BGApplier *bg_applier)
{
	GdkPixmap *pixmap;

	/* Only draw clean image if no pref set yet */
	if (bg_applier->p->last_prefs)
		return;       

	gtk_image_get_pixmap (GTK_IMAGE (preview), &pixmap, NULL);
	if (!pixmap) {
		pixmap = gdk_pixmap_new (preview->window,
					 bg_applier->p->render_geom.width,
					 bg_applier->p->render_geom.height,
					 -1);
		gtk_image_set_from_pixmap (GTK_IMAGE (preview), pixmap, NULL);
	}
	
	gdk_draw_rectangle (pixmap,
			    preview->style->bg_gc[preview->state],
			    TRUE,
			    bg_applier->p->render_geom.x,
			    bg_applier->p->render_geom.y,
			    bg_applier->p->render_geom.width,
			    bg_applier->p->render_geom.height);
}

