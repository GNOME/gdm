#include <math.h>
#include <gtk/gtk.h>
#include <librsvg/rsvg.h>

#include "greeter_item.h"
#include "greeter_canvas_item.h"
#include "greeter_configuration.h"

static void
apply_tint (GdkPixbuf *pixbuf, guint32 tint_color)
{
  guchar *pixels;
  guint r, g, b;
  gboolean has_alpha;
  guint w, h, stride;
  guint pixel_stride;
  guchar *line;
  int i;
  
  pixels = gdk_pixbuf_get_pixels (pixbuf);
  has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
  
  r = (tint_color & 0xff0000) >> 16;
  g = (tint_color & 0x00ff00) >> 8;
  b = (tint_color & 0x0000ff);

  w = gdk_pixbuf_get_width (pixbuf);
  h = gdk_pixbuf_get_height (pixbuf);
  stride = gdk_pixbuf_get_rowstride (pixbuf);

  pixel_stride = (has_alpha) ? 4 : 3;

  while (h-->0)
    {
      line = pixels;

      for (i = 0; i < w; i++)
	{
	  line[0] = line[0] * r / 0xff;
	  line[1] = line[1] * g / 0xff;
	  line[2] = line[2] * b / 0xff;
	  line += pixel_stride;
	}

      pixels += stride;
    }
}

static GdkPixbuf *
transform_pixbuf (GdkPixbuf *orig,
		  gboolean has_tint, guint32 tint_color,
		  double alpha, gint width, gint height)
{
  GdkPixbuf *scaled;
  gint p_width, p_height;

  p_width = gdk_pixbuf_get_width (orig);
  p_height = gdk_pixbuf_get_height (orig);
  
  if (p_width != width ||
      p_height != height ||
      alpha < 1.0 ||
      has_tint)
    {
      int alpha_i;
      
      if (alpha >= 1.0)
	alpha_i = 0xff;
      else if (alpha <= 0.0)
	alpha_i = 0;
      else
	alpha_i = (guint) floor (0xff*alpha);
      if (alpha != 0xff)
	{
	  scaled = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, width, height);
	  gdk_pixbuf_fill (scaled, 0);
	  gdk_pixbuf_composite (orig, scaled, 0, 0, width, height,
				0, 0, (double)width/p_width, (double)height/p_height,
				GDK_INTERP_BILINEAR, alpha_i);
	}
      else
	scaled = gdk_pixbuf_scale_simple (orig, width, height, GDK_INTERP_BILINEAR);
    }
  else
    scaled = g_object_ref (orig);
  
  if (has_tint)
    apply_tint (scaled, tint_color);

  return scaled;
}

void
greeter_item_create_canvas_item (GreeterItemInfo *item)
{
  GnomeCanvasGroup *group;
  GtkWidget *entry;
  double x1, y1, x2, y2;
  int i;
  GtkAllocation rect;
  char *text;

  if (item->item != NULL)
    return;

  if ( ! greeter_item_is_visible (item))
    return;

  g_assert (item->parent->group_item);
  
  if (item->fixed_children != NULL ||
      item->box_children != NULL)
    {
      item->group_item =
	(GnomeCanvasGroup *)gnome_canvas_item_new (item->parent->group_item,
						   GNOME_TYPE_CANVAS_GROUP,
						   "x", (gdouble) 0.0,
						   "y", (gdouble) 0.0,
						   NULL);
      group = item->group_item;
    }
  else
    group = item->parent->group_item;

  rect = item->allocation;

  
  x1 = (gdouble) rect.x;
  y1 = (gdouble) rect.y;
  x2 = (gdouble) rect.x + rect.width;
  y2 = (gdouble) rect.y + rect.height;

  switch (item->item_type) {
  case GREETER_ITEM_TYPE_RECT:
    item->item = gnome_canvas_item_new (group,
					GNOME_TYPE_CANVAS_RECT,
					"x1", x1,
					"y1", y1,
					"x2", x2,
					"y2", y2,
					"fill_color_rgba", item->colors[GREETER_ITEM_STATE_NORMAL],
					NULL);
    break;
  case GREETER_ITEM_TYPE_SVG:
    for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
      {
	if (item->files[i])
	  item->orig_pixbufs[i] =
	    rsvg_pixbuf_from_file_at_size (item->files[i],
					   rect.width, rect.height,
					   NULL);
	else
	  item->orig_pixbufs[i] = NULL;
      }

    /* Fall through */
  case GREETER_ITEM_TYPE_PIXMAP:
    for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
      {
	if (item->orig_pixbufs[i])
	  item->pixbufs[i] =
	    transform_pixbuf (item->orig_pixbufs[i],
			      item->have_tint[i], item->tints[i],
			      item->alphas[i], rect.width, rect.height);
	else
	  item->pixbufs[i] = NULL;
      }
    
    item->item = gnome_canvas_item_new (group,
					GNOME_TYPE_CANVAS_PIXBUF,
					"x", (gdouble) x1,
					"y", (gdouble) y1,
					"pixbuf", item->pixbufs[GREETER_ITEM_STATE_NORMAL],
					NULL);
    break;
  case GREETER_ITEM_TYPE_LABEL:
    text = greeter_item_expand_text (item->orig_text);

    item->item = gnome_canvas_item_new (group,
					GNOME_TYPE_CANVAS_TEXT,
					"text", text,
					"x", x1,
					"y", y1,
					"anchor", item->anchor,
					"font_desc", item->fonts[GREETER_ITEM_STATE_NORMAL],
					"fill_color_rgba", item->colors[GREETER_ITEM_STATE_NORMAL],
					NULL);
    g_free (text);
    
    break;
    
  case GREETER_ITEM_TYPE_ENTRY:
    entry = gtk_entry_new ();
    gtk_entry_set_has_frame (GTK_ENTRY (entry), FALSE);
    if (GdmUseCirclesInEntry)
      gtk_entry_set_invisible_char (GTK_ENTRY (entry), 0x25cf);
    
    item->item = gnome_canvas_item_new (group,
					GNOME_TYPE_CANVAS_WIDGET,
					"widget", entry,
					"x", x1,
					"y", y1,
					"height", (double)rect.height,
					"width", (double)rect.width,
					NULL);
    break;
  }

  if (item->item_type == GREETER_ITEM_TYPE_RECT ||
      item->item_type == GREETER_ITEM_TYPE_SVG ||
      item->item_type == GREETER_ITEM_TYPE_PIXMAP ||
      item->item_type == GREETER_ITEM_TYPE_LABEL)
    gtk_signal_connect (GTK_OBJECT (item->item), "event",
			(GtkSignalFunc) greeter_item_event_handler,
			item);
}

