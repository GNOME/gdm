#include <gtk/gtk.h>
#include <libxml/parser.h>
#include <string.h>
#include <stdlib.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <gdk/gdkx.h>

#include "greeter_parser.h"
#include "greeter_events.h"

static char *gdm_language = "se";

GHashTable *item_hash = NULL;

static gboolean parse_items (xmlNodePtr node,
			     GnomeCanvas *canvas,
			     GdkRectangle *parent_rect,
			     GnomeCanvasGroup *parent_group);

GreeterItemInfo *
greeter_lookup_id (const char *id)
{
  GreeterItemInfo key;
  GreeterItemInfo *info;

  key.id = (char *)id;
  info = g_hash_table_lookup (item_hash, &key);

  return info;
}

static gboolean
parse_id (xmlNodePtr node,
	  GreeterItemInfo *info)
{
  xmlChar *prop;
  
  prop = xmlGetProp (node, "id");
  
  if (prop)
    {
      info->id = g_strdup (prop);
      g_hash_table_insert (item_hash, info, info);
      xmlFree (prop);
    }
  
  return TRUE;
}

/* Doesn't set the parts of rect that are not specified.
 * If you want specific default values you need to fill them out
 * in rect first
 */
static gboolean
parse_pos (xmlNodePtr     node,
	   GdkRectangle  *parent_rect,
	   GdkRectangle  *rect,
	   GtkAnchorType *anchor)
{
  xmlChar *prop;
  double val;
  
  *anchor = GTK_ANCHOR_NW;
 
  prop = xmlGetProp (node, "anchor");
  if (prop)
    {
      if (strcmp (prop, "center") == 0)
	*anchor = GTK_ANCHOR_CENTER;
      else if (strcmp (prop, "c") == 0)
	*anchor = GTK_ANCHOR_CENTER;
      else if (strcmp (prop, "nw") == 0)
	*anchor = GTK_ANCHOR_NW;
      else if (strcmp (prop, "n") == 0)
	*anchor = GTK_ANCHOR_N;
      else if (strcmp (prop, "ne") == 0)
	*anchor = GTK_ANCHOR_NE;
      else if (strcmp (prop, "w") == 0)
	*anchor = GTK_ANCHOR_W;
      else if (strcmp (prop, "e") == 0)
	*anchor = GTK_ANCHOR_E;
      else if (strcmp (prop, "sw") == 0)
	*anchor = GTK_ANCHOR_SW;
      else if (strcmp (prop, "s") == 0)
	*anchor = GTK_ANCHOR_S;
      else if (strcmp (prop, "se") == 0)
	*anchor = GTK_ANCHOR_SE;
      else
	{
	  g_warning ("Unknown anchor type %s\n", prop);
	  return FALSE;
	}
    }
   
  prop = xmlGetProp (node, "x");
  if (prop)
    {
      val = atof (prop);

      if (strchr (prop, '%') != NULL)
	rect->x = val * parent_rect->width / 100.0;
      else if (strchr (prop, '-') != NULL)
	rect->x = parent_rect->width + val;
      else
	rect->x = val;
    }
  
  prop = xmlGetProp (node, "y");
  if (prop)
    {
      val = atof (prop);

      if (strchr (prop, '%') != NULL)
	rect->y = val * parent_rect->height / 100.0;
      else if (strchr (prop, '-') != NULL)
	rect->y = parent_rect->height + val;
      else
	rect->y = val;
    }

  prop = xmlGetProp (node, "width");
  if (prop)
    {
      val = atof (prop);

      if (strchr (prop, '%') != NULL)
	rect->width = val * parent_rect->width / 100.0;
      else if (strchr (prop, '-') != NULL)
	rect->width = parent_rect->width - rect->x + val;
      else
	rect->width = val;
    }
  
  prop = xmlGetProp (node, "height");
  if (prop)
    {
      val = atof (prop);

      if (strchr (prop, '%') != NULL)
	rect->height = val * parent_rect->height / 100.0;
      else if (strchr (prop, '-') != NULL)
	rect->height = parent_rect->height - rect->y + val;
      else
	rect->height = val;
    }

  rect->x += parent_rect->x;
  rect->y += parent_rect->y;
  
  return TRUE;
}

void
fixup_from_anchor (GdkRectangle *rect, GtkAnchorType anchor)
{
  switch (anchor)
    {
    case GTK_ANCHOR_NW:
      break;
    case GTK_ANCHOR_N:
      rect->x -= rect->width/2;
      break;
    case GTK_ANCHOR_NE:
      rect->x -= rect->width;
      break;
      
    case GTK_ANCHOR_W:
      rect->y -= rect->height/2;
      break;
    case GTK_ANCHOR_CENTER:
      rect->x -= rect->width/2;
      rect->y -= rect->height/2;
      break;
    case GTK_ANCHOR_E:
      rect->x -= rect->width;
      rect->y -= rect->height/2;
      break;
      
    case GTK_ANCHOR_SW:
      rect->y -= rect->height;
      break;
    case GTK_ANCHOR_S:
      rect->x -= rect->width/2;
      rect->y -= rect->height;
      break;
    case GTK_ANCHOR_SE:
      rect->x -= rect->width;
      rect->y -= rect->height;
      break;
    }
}


guint32
parse_color (const char *str)
{
  guint32 col;
  int i;
  if (str[0] != '#')
    {
      g_warning ("colors must start with #\n");
      return 0;
    }
  if (strlen (str) != 7)
    {
      g_warning ("Colors must be on the format #xxxxxx\n");
      return 0;
    }

  col = 0;

  for (i = 0; i < 6; i++)
    col = (col << 4)  | g_ascii_xdigit_value (str[i+1]);

  return col;
}

static gboolean
parse_state_file (xmlNodePtr node,
		  char **filename,
		  gboolean *has_tint,
		  guint32  *tint_color,
		  gdouble  *alpha)
{
  xmlChar *prop;
  
  *filename = NULL;
  prop = xmlGetProp (node, "file");
  if (prop)
    *filename = prop;
  
  *has_tint = FALSE;
  prop = xmlGetProp (node, "tint");
  if (prop)
    {
      *tint_color = parse_color (prop);
      *has_tint = TRUE;
    }

  *alpha = 1.0;
  prop = xmlGetProp (node, "alpha");
  if (prop)
    *alpha = atof (prop);
  
  return TRUE;
}

static gboolean
parse_state_color (xmlNodePtr node,
		   guint32  *rgb_color,
		   gdouble  *alpha)
{
  xmlChar *prop;
  
  *rgb_color = 0;
  prop = xmlGetProp (node, "color");
  if (prop)
    *rgb_color = parse_color (prop);

  *alpha = 1.0;
  prop = xmlGetProp (node, "alpha");
  if (prop)
    *alpha = atof (prop);
  
  return TRUE;
}


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
load_scaled_pixbuf (char *filename, gboolean is_svg,
		    gboolean has_tint, guint32 tint_color,
		    double alpha, gint width, gint height)
{
  GdkPixbuf *orig, *scaled;
  gint p_width, p_height;

  if (is_svg)
    {
      /* FIXME: Handle width < 0 or height < 0 */
      orig = rsvg_pixbuf_from_file_at_size (filename,
					    width, height,
					    NULL);
      if (orig == NULL)
	return NULL;
    }
  else
    {
      orig = gdk_pixbuf_new_from_file (filename, NULL);

      if (orig == NULL)
	return NULL;
    }

  p_width = gdk_pixbuf_get_width (orig);
  p_height = gdk_pixbuf_get_height (orig);
  
  if (width < 0)
    width = p_width;
  if (height < 0)
    height = p_height;
  
  if (p_width != width ||
      p_height != height ||
      alpha < 1.0)
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
      
      gdk_pixbuf_unref (orig);
    }
  else
    scaled = orig;
  
  if (has_tint)
    apply_tint (scaled, tint_color);

  return scaled;
}

static gboolean
parse_pixmap (xmlNodePtr        node,
	      GnomeCanvas      *canvas,
	      GdkRectangle     *parent_rect,
	      GnomeCanvasGroup *parent_group,
	      gboolean          svg,
	      GreeterItemInfo **ret_info)
{
  xmlNodePtr child;
  xmlNodePtr child_items = NULL;
  gboolean res;
  char *filename[GREETER_ITEM_STATE_MAX] = {NULL};
  double alpha[GREETER_ITEM_STATE_MAX];
  gboolean has_tint[GREETER_ITEM_STATE_MAX];
  guint32 tint[GREETER_ITEM_STATE_MAX];
  GdkRectangle pos;
  GtkAnchorType anchor;
  GreeterItemInfo *info;
  GnomeCanvasGroup *group;
  int i;
		
  child = node->children;

  info = greeter_item_info_new ();
  
  pos.x = 0;
  pos.y = 0;
  pos.width = -1;
  pos.height = -1;
  anchor = GTK_ANCHOR_NW;
  
  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  res = parse_state_file (child,
				  &filename[GREETER_ITEM_STATE_NORMAL],
				  &has_tint[GREETER_ITEM_STATE_NORMAL],
				  &tint[GREETER_ITEM_STATE_NORMAL],
				  &alpha[GREETER_ITEM_STATE_NORMAL]);
	  
	  if (!res)
	    return FALSE;
	  
	  if (!filename[GREETER_ITEM_STATE_NORMAL])
	    {
	      g_warning ("Did not specify filename for state\n");
	      return FALSE;
	    }
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  res = parse_state_file (child,
				  &filename[GREETER_ITEM_STATE_PRELIGHT],
				  &has_tint[GREETER_ITEM_STATE_PRELIGHT],
				  &tint[GREETER_ITEM_STATE_PRELIGHT],
				  &alpha[GREETER_ITEM_STATE_PRELIGHT]);
	  
	  if (!res)
	    return FALSE;
	}
      else if (strcmp (child->name, "active") == 0)
	{
	  res = parse_state_file (child,
				  &filename[GREETER_ITEM_STATE_ACTIVE],
				  &has_tint[GREETER_ITEM_STATE_ACTIVE],
				  &tint[GREETER_ITEM_STATE_ACTIVE],
				  &alpha[GREETER_ITEM_STATE_ACTIVE]);
	  
	  if (!res)
	    return FALSE;
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  res = parse_pos (child, parent_rect, &pos, &anchor);
	  if (!res)
	    return FALSE;
	}
      else if (strcmp (child->name, "children") == 0)
	{
	  child_items = child;
	  if (!res)
	    return FALSE;
	}
      
      child = child->next;
    }

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (filename[i])
	info->pixbufs[i] = load_scaled_pixbuf (filename[i], svg,
					       has_tint[i], tint[i],
					       alpha[i],
					       pos.width, pos.height);
      else
	info->pixbufs[i] = NULL;
	
    }

  if (info->pixbufs[GREETER_ITEM_STATE_NORMAL] == NULL)
    return FALSE;
  
  if (pos.width < 0)
    pos.width = gdk_pixbuf_get_width (info->pixbufs[GREETER_ITEM_STATE_NORMAL]);
  
  if (pos.height < 0)
    pos.height = gdk_pixbuf_get_height (info->pixbufs[GREETER_ITEM_STATE_NORMAL]);
  
  fixup_from_anchor (&pos, anchor);

  if (child_items)
    {
      info->group_item = gnome_canvas_item_new (parent_group,
						GNOME_TYPE_CANVAS_GROUP,
						"x", (gdouble) 0.0,
						"y", (gdouble) 0.0,
						NULL);
      group = info->group_item;

    }
  else
    group = parent_group;
  
  info->item = gnome_canvas_item_new (group,
				      GNOME_TYPE_CANVAS_PIXBUF,
				      "x", (gdouble) pos.x,
				      "y", (gdouble) pos.y,
				      "anchor", GTK_ANCHOR_NW,
				      "pixbuf", info->pixbufs[GREETER_ITEM_STATE_NORMAL],
				      NULL);

  *ret_info = info;
  
  gtk_signal_connect (GTK_OBJECT (info->item), "event",
		      (GtkSignalFunc) greeter_item_event_handler,
		      info);

  if (child_items)
    return parse_items (child_items, canvas, &pos, group);
  
  return TRUE;
}

static gboolean
parse_rect (xmlNodePtr node,
	    GnomeCanvas *canvas,
	    GdkRectangle *parent_rect,
	    GnomeCanvasGroup *parent_group,
	    GreeterItemInfo **ret_info)
{
  xmlNodePtr child;
  xmlNodePtr child_items = NULL;
  gboolean res = TRUE;
  double alpha[GREETER_ITEM_STATE_MAX];
  GdkRectangle pos;
  GtkAnchorType anchor;
  GreeterItemInfo *info;
  GnomeCanvasGroup *group;
  int i;
  
  child = node->children;
  
  pos.x = 0;
  pos.y = 0;
  pos.width = -1;
  pos.height = -1;
  anchor = GTK_ANCHOR_NW;

  info = greeter_item_info_new ();

  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  res = parse_state_color (child,
				   &info->colors[GREETER_ITEM_STATE_NORMAL],
				   &alpha[GREETER_ITEM_STATE_NORMAL]);
	  info->have_color[GREETER_ITEM_STATE_NORMAL] = TRUE;
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  res = parse_state_color (child,
				   &info->colors[GREETER_ITEM_STATE_PRELIGHT],
				   &alpha[GREETER_ITEM_STATE_PRELIGHT]);
	  info->have_color[GREETER_ITEM_STATE_PRELIGHT] = TRUE;
	}
      else if (strcmp (child->name, "active") == 0)
	{
	  res = parse_state_color (child,
				   &info->colors[GREETER_ITEM_STATE_ACTIVE],
				   &alpha[GREETER_ITEM_STATE_ACTIVE]);
	  info->have_color[GREETER_ITEM_STATE_ACTIVE] = TRUE;
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  res = parse_pos (child, parent_rect, &pos, &anchor);
	}
      else if (strcmp (child->name, "children") == 0)
	{
	  child_items = child;
	}
	  
      if (!res)
	return FALSE;
      
      child = child->next;
    }

  if (pos.width < 0 || pos.height < 0)
    {
      g_warning ("Must specify width and height of rect\n");
      return FALSE;
    }

  fixup_from_anchor (&pos, anchor);

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (!info->have_color[i])
	continue;
      
      if (alpha[i] >= 1.0)
	info->colors[i] = (info->colors[i] << 8) | 0xff;
      else if (alpha[i] > 0)
	info->colors[i] = (info->colors[i] << 8) | (guint) floor (0xff*alpha[i]);
      else
	info->colors[i] = 0;
    }

  if (child_items)
    {
      info->group_item = gnome_canvas_item_new (parent_group,
						GNOME_TYPE_CANVAS_GROUP,
						"x", (gdouble) 0.0,
						"y", (gdouble) 0.0,
						NULL);
      group = info->group_item;

    }
  else
    group = parent_group;

  
  info->item = gnome_canvas_item_new (group,
				      GNOME_TYPE_CANVAS_RECT,
				      "x1", (gdouble) pos.x,
				      "y1", (gdouble) pos.y,
				      "x2", (gdouble) pos.x + pos.width,
				      "y2", (gdouble) pos.y + pos.height,
				      "fill_color_rgba", info->colors[GREETER_ITEM_STATE_NORMAL],
				      NULL);

  *ret_info = info;
  
  gtk_signal_connect (GTK_OBJECT (info->item), "event",
		      (GtkSignalFunc) greeter_item_event_handler,
		      info);

  if (child_items)
    return parse_items (child_items, canvas, &pos, group);
  
  return TRUE;
}


static gboolean
parse_state_text (xmlNodePtr node,
		  char     **font,
		  guint32   *rgb_color,
		  gdouble   *alpha)
{
  xmlChar *prop;

  *font = NULL;
  prop = xmlGetProp (node, "font");
  if (prop)
    *font = prop;

  *rgb_color = 0;
  prop = xmlGetProp (node, "color");
  if (prop)
    *rgb_color = parse_color (prop);

  *alpha = 1.0;
  prop = xmlGetProp (node, "alpha");
  if (prop)
    *alpha = atof (prop);
  
  return TRUE;
}

static gboolean
parse_translated_text (xmlNodePtr node,
		       char **default_text,
		       char **translated_text)
{
  xmlChar *prop;
  gboolean translated = FALSE;
  
  prop = xmlGetProp (node, "lang");
  if (prop)
    {
      if (strcmp (prop, gdm_language) == 0)
	translated = TRUE;
      else
	{
	  xmlFree (prop);
	  return TRUE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "val");
  if (prop == NULL)
    {
      xmlFree (prop);
      g_warning ("text node missing value\n");
      return FALSE;
    }
  
  if (translated)
    *translated_text = g_strdup (prop);
  else
    *default_text = g_strdup (prop);
  
  xmlFree (prop);
  return TRUE;
}


static gboolean
parse_label (xmlNodePtr        node,
	     GnomeCanvas      *canvas,
	     GdkRectangle     *parent_rect,
	     GnomeCanvasGroup *parent_group,
	     GreeterItemInfo **ret_info)
{
  xmlNodePtr child;
  xmlNodePtr child_items = NULL;
  gboolean res = TRUE;
  char *text;
  char *default_text = NULL;
  char *translated_text = NULL;
  char *normal_font;
  guint32 normal_color;
  double normal_alpha;
  char *pre_font;
  guint32 pre_color;
  double pre_alpha;
  GdkRectangle pos;
  GtkAnchorType anchor;
  GreeterItemInfo *info;
  
  child = node->children;

  info = greeter_item_info_new ();

  pos.x = 0;
  pos.y = 0;
  pos.width = -1;
  pos.height = -1;
  anchor = GTK_ANCHOR_NW;

  normal_alpha = 1.0;
  pre_alpha = 1.0;
  
  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  res = parse_state_text (child, &normal_font,
				  &normal_color, &normal_alpha);
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  res = parse_state_text (child, &pre_font,
				  &pre_color, &pre_alpha);
	  
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  res = parse_pos (child, parent_rect, &pos, &anchor);
	}
      else if (child->type == XML_ELEMENT_NODE &&
	       strcmp (child->name, "text") == 0)
	{
	  res = parse_translated_text (child, &default_text, &translated_text);
	}
      else if (strcmp (child->name, "children") == 0)
	{
	  g_warning ("label item cannot have children\n");
	  return FALSE;
	}
	  
      if (!res)
	return FALSE;
      
      child = child->next;
    }

  if (default_text == NULL &&
      translated_text == NULL)
    {
      g_warning ("A label must specify the text attribute\n");
      return FALSE;
    }
  

  if (normal_alpha >= 1.0)
    normal_color = (normal_color << 8) | 0xff;
  else if (normal_alpha > 0)
    normal_color = (normal_color << 8) | (guint) floor (0xff*normal_alpha);

  if (!normal_font)
    normal_font = "Sans";

  if (translated_text)
    {
      info->orig_text = translated_text;
      g_free (default_text);
    }
  else
    info->orig_text = default_text;
  
  text = greeter_item_expand_text (info->orig_text);
  
  info->item = gnome_canvas_item_new (parent_group,
				      GNOME_TYPE_CANVAS_TEXT,
				      "text", text,
				      "x", (gdouble) pos.x,
				      "y", (gdouble) pos.y,
				      "anchor", anchor,
				      "font", normal_font,
				      "fill_color_rgba", normal_color,
				      NULL);

  g_free (text);
  
  *ret_info = info;
  
  /* FIXME: Implement text prelighting */

  return TRUE;
}

static gboolean
parse_entry (xmlNodePtr        node,
	     GnomeCanvas      *canvas,
	     GdkRectangle     *parent_rect,
	     GnomeCanvasGroup *parent_group,
	     GreeterItemInfo **ret_info)
{
  xmlNodePtr child;
  gboolean res = TRUE;
  GdkRectangle pos;
  GtkAnchorType anchor;
  GtkWidget *entry;
  GreeterItemInfo *info;

  child = node->children;
  
  info = greeter_item_info_new ();
  
  pos.x = 0;
  pos.y = 0;
  pos.width = -1;
  pos.height = -1;
  anchor = GTK_ANCHOR_NW;

  while (child)
    {
      if (strcmp (child->name, "pos") == 0)
	{
	  res = parse_pos (child, parent_rect, &pos, &anchor);
	}
      else if (strcmp (child->name, "children") == 0)
	{
	  g_warning ("label item cannot have children\n");
	  return FALSE;
	}
	  
      if (!res)
	return FALSE;
      
      child = child->next;
    }

  entry = gtk_entry_new ();
  gtk_entry_set_has_frame (GTK_ENTRY (entry), FALSE);

  info->item = gnome_canvas_item_new (parent_group,
				      GNOME_TYPE_CANVAS_WIDGET,
				      "widget", entry,
				      "x", (gdouble) pos.x,
				      "y", (gdouble) pos.y,
				      "height", (gdouble) pos.height,
				      "width", (gdouble) pos.width,
				      "anchor", anchor,
				      NULL);

  *ret_info = info;

  return TRUE;
}

static gboolean
parse_items (xmlNodePtr node,
	     GnomeCanvas *canvas,
	     GdkRectangle *parent_rect,
	     GnomeCanvasGroup *parent_group)
{
    xmlNodePtr child;
    gboolean res;
    xmlChar *type;
    GreeterItemInfo *info;
    
    child = node->children;
    
    while (child)
      {
	if (child->type == XML_ELEMENT_NODE)
	  {
	    if (strcmp (child->name, "item") != 0)
	      {
		g_warning ("Parse error: found tag %s when looking for item\n", child->name);
		return FALSE;
	      }
	    
	    type = xmlGetProp (child, "type");

	    if (!type)
	      {
		g_warning ("Parse error: items must specify their type");
		return FALSE;
	      }
	    
	    res = TRUE;
	    if (strcmp (type, "svg") == 0)
	      res = parse_pixmap (child, canvas, parent_rect, parent_group, TRUE, &info);
	    else if (strcmp (type, "pixmap") == 0)
	      res = parse_pixmap (child, canvas, parent_rect, parent_group, FALSE, &info);
	    else if (strcmp (type, "rect") == 0)
	      res = parse_rect (child, canvas, parent_rect, parent_group, &info);
	    else if (strcmp (type, "label") == 0)
	      res = parse_label (child, canvas, parent_rect, parent_group, &info);
	    else if (strcmp (type, "entry") == 0)
	      res = parse_entry (child, canvas, parent_rect, parent_group, &info);
	    
	    if (!res)
	      return FALSE;

	    parse_id (child, info);
	  }
	child = child->next;
      }
    return TRUE;
}

static gboolean
hook_up_items (GnomeCanvas *canvas)
{
  return TRUE;
}

static gboolean
greeter_info_id_equal (GreeterItemInfo *a,
		       GreeterItemInfo *b)
{
  return g_str_equal (a->id, b->id);
}

static guint
greeter_info_id_hash (GreeterItemInfo *key)
{
  return g_str_hash (key->id);
}

gboolean
greeter_parse (char *file, GnomeCanvas *canvas,
	       int width, int height)
{
  GdkRectangle parent_rect;
  xmlDocPtr doc;
  xmlNodePtr node;
  gboolean retval;

  doc = xmlParseFile (file);
  if (doc == NULL)
    return FALSE;
  
  node = xmlDocGetRootElement (doc);
  if (node == NULL)
    return FALSE;
  
  item_hash = g_hash_table_new_full ((GHashFunc)greeter_info_id_hash,
				     (GEqualFunc)greeter_info_id_equal,
				     NULL,
				     (GDestroyNotify)greeter_item_info_free);
  
  g_assert (strcmp (node->name, "greeter") == 0);
  
  parent_rect.x = 0;
  parent_rect.y = 0;
  parent_rect.width = width;
  parent_rect.height = height;

  retval =  parse_items (node, canvas, &parent_rect, gnome_canvas_root (canvas));
  if (retval)
    retval = hook_up_items (canvas);

  return retval;
}
