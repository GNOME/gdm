#include <gtk/gtk.h>
#include <libxml/parser.h>
#include <string.h>
#include <stdlib.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <gdk/gdkx.h>
#include <locale.h>
#include <libgnome/gnome-i18n.h>

#include "greeter_parser.h"
#include "greeter_events.h"

static char *file_search_path = NULL;

GHashTable *item_hash = NULL;

static gboolean parse_items (xmlNodePtr       node,
			     GList          **items_out,
			     GreeterItemInfo *parent,
			     GError         **error);


GQuark
greeter_parser_error_quark (void)
{
  static GQuark quark;
  if (!quark)
    quark = g_quark_from_static_string ("greeter_parser_error");

  return quark;
}


GreeterItemInfo *
greeter_lookup_id (const char *id)
{
  GreeterItemInfo key;
  GreeterItemInfo *info;

  key.id = (char *)id;
  info = g_hash_table_lookup (item_hash, &key);

  return info;
}

static void
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
}

/* Doesn't set the parts of rect that are not specified.
 * If you want specific default values you need to fill them out
 * in rect first
 */
static gboolean
parse_pos (xmlNodePtr       node,
	   GreeterItemInfo *info,
	   GError         **error)
{
  xmlChar *prop;
  char *p;
  
  prop = xmlGetProp (node, "anchor");
  if (prop)
    {
      if (strcmp (prop, "center") == 0)
	info->anchor = GTK_ANCHOR_CENTER;
      else if (strcmp (prop, "c") == 0)
	info->anchor = GTK_ANCHOR_CENTER;
      else if (strcmp (prop, "nw") == 0)
	info->anchor = GTK_ANCHOR_NW;
      else if (strcmp (prop, "n") == 0)
	info->anchor = GTK_ANCHOR_N;
      else if (strcmp (prop, "ne") == 0)
	info->anchor = GTK_ANCHOR_NE;
      else if (strcmp (prop, "w") == 0)
	info->anchor = GTK_ANCHOR_W;
      else if (strcmp (prop, "e") == 0)
	info->anchor = GTK_ANCHOR_E;
      else if (strcmp (prop, "sw") == 0)
	info->anchor = GTK_ANCHOR_SW;
      else if (strcmp (prop, "s") == 0)
	info->anchor = GTK_ANCHOR_S;
      else if (strcmp (prop, "se") == 0)
	info->anchor = GTK_ANCHOR_SE;
      else
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Unknown anchor type %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
   
  prop = xmlGetProp (node, "x");
  if (prop)
    {
      info->x = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad position specifier%s\n", prop);
	  return FALSE;
	}
      
      if (strchr (prop, '%') != NULL)
	info->x_type = GREETER_ITEM_POS_RELATIVE;
      else 
	info->x_type = GREETER_ITEM_POS_ABSOLUTE;
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "y");
  if (prop)
    {
      info->y = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad position specifier%s\n", prop);
	  return FALSE;
	}
      
      if (strchr (prop, '%') != NULL)
	info->y_type = GREETER_ITEM_POS_RELATIVE;
      else 
	info->y_type = GREETER_ITEM_POS_ABSOLUTE;
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "width");
  if (prop)
    {
      if (strcmp (prop, "box") == 0)
	info->width_type = GREETER_ITEM_SIZE_BOX;
      else
	{
	  info->width = g_ascii_strtod (prop, &p);
      
	  if ((char *)prop == p)
	    {
	      *error = g_error_new (GREETER_PARSER_ERROR,
				    GREETER_PARSER_ERROR_BAD_SPEC,
				    "Bad size specifier%s\n", prop);
	      return FALSE;
	    }
      
	  if (strchr (prop, '%') != NULL)
	    info->width_type = GREETER_ITEM_SIZE_RELATIVE;
	  else 
	    info->width_type = GREETER_ITEM_SIZE_ABSOLUTE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "height");
  if (prop)
    {
      if (strcmp (prop, "box") == 0)
	info->height_type = GREETER_ITEM_SIZE_BOX;
      else
	{
	  info->height = g_ascii_strtod (prop, &p);
      
	  if ((char *)prop == p)
	    {
	      *error = g_error_new (GREETER_PARSER_ERROR,
				    GREETER_PARSER_ERROR_BAD_SPEC,
				    "Bad size specifier%s\n", prop);
	      return FALSE;
	    }
      
	  if (strchr (prop, '%') != NULL)
	    info->height_type = GREETER_ITEM_SIZE_RELATIVE;
	  else 
	    info->height_type = GREETER_ITEM_SIZE_ABSOLUTE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "expand");
  if (prop)
    {
      if (strcmp (prop, "true") == 0)
	{
	  info->expand = TRUE;
	}
      else if (strcmp (prop, "false") == 0)
	{
	  info->expand = FALSE;
	}
      else
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"bad expand spec %s\n", prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }

  return TRUE;
}

static gboolean
parse_fixed (xmlNodePtr       node,
	     GreeterItemInfo *info,
	     GError         **error)
{
  return parse_items (node,
		      &info->fixed_children,
		      info,
		      error);
}

static gboolean
parse_box (xmlNodePtr       node,
	   GreeterItemInfo *info,
	   GError         **error)
{
  xmlChar *prop;
  char *p;
  
  prop = xmlGetProp (node, "orientation");
  if (prop)
    {
      if (strcmp (prop, "horizontal") == 0)
	{
	  info->box_orientation = GTK_ORIENTATION_HORIZONTAL;
	}
      else if (strcmp (prop, "vertical") == 0)
	{
	  info->box_orientation = GTK_ORIENTATION_VERTICAL;
	}
      else
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"bad orientation %s\n", prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "homogeneous");
  if (prop)
    {
      if (strcmp (prop, "true") == 0)
	{
	  info->box_homogeneous = TRUE;
	}
      else if (strcmp (prop, "false") == 0)
	{
	  info->box_homogeneous = FALSE;
	}
      else
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"bad homogenous spec %s\n", prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }


  prop = xmlGetProp (node, "xpadding");
  if (prop)
    {
      info->box_x_padding = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad padding specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "ypadding");
  if (prop)
    {
      info->box_y_padding = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad padding specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "min-width");
  if (prop)
    {
      info->box_min_width = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad min-width specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "min-height");
  if (prop)
    {
      info->box_min_height = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad min-height specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "spacing");
  if (prop)
    {
      info->box_spacing = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad spacing specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }

  return parse_items (node,
		      &info->box_children,
		      info,
		      error);

}


static gboolean
parse_color (const char *str,
	     guint32 *col_out,
	     GError **error)
{
  guint32 col;
  int i;
  if (str[0] != '#')
    {
      *error = g_error_new (GREETER_PARSER_ERROR,
			    GREETER_PARSER_ERROR_BAD_SPEC,
			    "colors must start with #, %s is an invalid color\n", str);
      return FALSE;
    }
  if (strlen (str) != 7)
    {
      *error = g_error_new (GREETER_PARSER_ERROR,
			    GREETER_PARSER_ERROR_BAD_SPEC,
			    "Colors must be on the format #xxxxxx, %s is an invalid color\n", str);
      return FALSE;
    }

  col = 0;

  for (i = 0; i < 6; i++)
    col = (col << 4)  | g_ascii_xdigit_value (str[i+1]);

  *col_out = col;

  return TRUE;
}

static gboolean
parse_state_file (xmlNodePtr node,
		  GreeterItemInfo  *info,
		  GreeterItemState state,
		  GError         **error)
{
  xmlChar *prop;
  char *p;
  
  prop = xmlGetProp (node, "file");
  if (prop)
    {
      if (g_path_is_absolute (prop))
	info->files[state] = g_strdup (prop);
      else
	info->files[state] = g_build_filename (file_search_path,
					       prop,
					       NULL);
	
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "tint");
  if (prop)
    {
      if (!parse_color (prop, &info->tints[state], error))
	return FALSE;
      info->have_tint[state] = TRUE;
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "alpha");
  if (prop)
    {
      info->alphas[state] = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad alpha specifier format %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  return TRUE;
}

static gboolean
parse_state_color (xmlNodePtr node,
		   GreeterItemInfo  *info,
		   GreeterItemState state,
		   GError         **error)
{
  xmlChar *prop;
  char *p;
  
  prop = xmlGetProp (node, "color");
  if (prop)
    {
      if (!parse_color (prop, &info->colors[state], error))
	return FALSE;
      info->have_color[state] = TRUE;
      xmlFree (prop);
    }

  prop = xmlGetProp (node, "alpha");
  if (prop)
    {
      info->alphas[state] = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad alpha specifier format %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  return TRUE;
}

static gboolean
parse_pixmap (xmlNodePtr        node,
	      gboolean          svg,
	      GreeterItemInfo  *info,
	      GError          **error)
{
  xmlNodePtr child;
  int i;
		
  child = node->children;

  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  if (!parse_state_file (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  if (!parse_state_file (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "active") == 0)
	{
	  if (!parse_state_file (child, info, GREETER_ITEM_STATE_ACTIVE, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  if (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "fixed") == 0)
	{
	  if (!parse_fixed (child, info, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "box") == 0)
	{
	  if (!parse_box (child, info, error))
	    return FALSE;
	}
      
      child = child->next;
    }

  if (!info->files[GREETER_ITEM_STATE_NORMAL])
    {
      *error = g_error_new (GREETER_PARSER_ERROR,
			    GREETER_PARSER_ERROR_BAD_SPEC,
			    "No filename specified for normal state\n");
      return FALSE;
    }
  
  if (!svg)
    {
      for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
	{
	  if (info->files[i] != NULL)
	    {
	      info->orig_pixbufs[i] = gdk_pixbuf_new_from_file (info->files[i], error);
	      
	      if (info->orig_pixbufs[i] == NULL)
		return FALSE;
	    }
	  else
	    info->orig_pixbufs[i] = NULL;
	}
    }

  return TRUE;
}

static gboolean
parse_rect (xmlNodePtr node,
	    GreeterItemInfo  *info,
	    GError          **error)
{
  xmlNodePtr child;
  int i;
  
  child = node->children;
  
  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  if (!parse_state_color (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  if (!parse_state_color (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "active") == 0)
	{
	  if (!parse_state_color (child, info, GREETER_ITEM_STATE_ACTIVE, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  if (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "fixed") == 0)
	{
	  if (!parse_fixed (child, info, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "box") == 0)
	{
	  if (!parse_box (child, info, error))
	    return FALSE;
	}
      
      child = child->next;
    }

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (!info->have_color[i])
	continue;
      
      if (info->alphas[i] >= 1.0)
	info->colors[i] = (info->colors[i] << 8) | 0xff;
      else if (info->alphas[i] > 0)
	info->colors[i] = (info->colors[i] << 8) | (guint) floor (0xff*info->alphas[i]);
      else
	info->colors[i] = 0;
    }
  
  return TRUE;
}


static gboolean
parse_state_text (xmlNodePtr node,
		  GreeterItemInfo  *info,
		  GreeterItemState state,
		  GError         **error)
{
  xmlChar *prop;
  char *p;

  prop = xmlGetProp (node, "font");
  if (prop)
    {
      info->fonts[state] = pango_font_description_from_string (prop);
      if (info->fonts[state] == NULL)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad font specification %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node, "color");
  if (prop)
    {
      if (!parse_color (prop, &info->colors[state], error))
	return FALSE;
      info->have_color[state] = TRUE;
      xmlFree (prop);
   }

  prop = xmlGetProp (node, "alpha");
  if (prop)
    {
      info->alphas[state] = g_ascii_strtod (prop, &p);
      
      if ((char *)prop == p)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"Bad alpha specifier format %s\n", prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  return TRUE;
}

static gint
is_current_locale (const char *lang)
{
  const GList *l = gnome_i18n_get_language_list ("LC_MESSAGES");
  int score = 0;
  
  while (l != NULL)
    {
      if (strcmp (l->data, lang) == 0)
	return score;

      l = l->next;
      score++;
    }
  return 1000;
}

static gboolean
parse_translated_text (xmlNodePtr node,
		       char     **translated_text,
		       gint      *translation_score,
		       GError   **error)
{
  xmlChar *prop;
  gint score;
  
  prop = xmlGetProp (node, "lang");
  if (prop)
    {
      score = is_current_locale (prop);
      xmlFree (prop);
    } else
      score = 999;

  if (score >= *translation_score)
    return TRUE;
  
  prop = xmlGetProp (node, "val");
  if (prop == NULL)
    {
      *error = g_error_new (GREETER_PARSER_ERROR,
			    GREETER_PARSER_ERROR_BAD_SPEC,
			    "No string defined for text node\n");
      return FALSE;
    }

  *translation_score = score;
  if (*translated_text)
    g_free (*translated_text);
  *translated_text = g_strdup (prop);
  
  xmlFree (prop);
  
  return TRUE;
}


static gboolean
parse_label (xmlNodePtr        node,
	     GreeterItemInfo  *info,
	     GError         **error)
{
  xmlNodePtr child;
  int i;
  char *translated_text;
  gint translation_score = 1000;
  
  translated_text = NULL;
  
  child = node->children;
  while (child)
    {
      if (strcmp (child->name, "normal") == 0)
	{
	  if (!parse_state_text (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "prelight") == 0)
	{
	  if (!parse_state_text (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "pos") == 0)
	{
	  if (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (child->type == XML_ELEMENT_NODE &&
	       strcmp (child->name, "text") == 0)
	{
	  if (!parse_translated_text (child, &translated_text, &translation_score, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "fixed") == 0 ||
	       strcmp (child->name, "boxed") == 0)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"label items cannot have children");
	  return FALSE;
	}
	  
      child = child->next;
    }

  if (translated_text == NULL)
    {
      *error = g_error_new (GREETER_PARSER_ERROR,
			    GREETER_PARSER_ERROR_BAD_SPEC,
			    "A label must specify the text attribute");
    }

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (!info->have_color[i])
	continue;
      
      if (info->alphas[i] >= 1.0)
	info->colors[i] = (info->colors[i] << 8) | 0xff;
      else if (info->alphas[i] > 0)
	info->colors[i] = (info->colors[i] << 8) | (guint) floor (0xff*info->alphas[i]);
      else
	info->colors[i] = 0;
    }
  
  if (info->fonts[GREETER_ITEM_STATE_NORMAL] == NULL)
    info->fonts[GREETER_ITEM_STATE_NORMAL] = pango_font_description_from_string ("Sans");

  info->orig_text = translated_text;
  
  return TRUE;
}

static gboolean
parse_entry (xmlNodePtr        node,
	     GreeterItemInfo  *info,
	     GError         **error)
{
  xmlNodePtr child;

  child = node->children;
  while (child)
    {
      if (strcmp (child->name, "pos") == 0)
	{
	  if (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp (child->name, "fixed") == 0 ||
	       strcmp (child->name, "boxed") == 0)
	{
	  *error = g_error_new (GREETER_PARSER_ERROR,
				GREETER_PARSER_ERROR_BAD_SPEC,
				"entry items cannot have children");
	  return FALSE;
	}
    
      child = child->next;
    }

  return TRUE;
}

static gboolean
parse_items (xmlNodePtr  node,
	     GList     **items_out,
	     GreeterItemInfo *parent,
	     GError    **error)
{
    xmlNodePtr child;
    GList *items;
    gboolean res;
    xmlChar *type;
    GreeterItemInfo *info;
    GreeterItemType item_type;
    
    items = NULL;
    
    child = node->children;
    while (child)
      {
	if (child->type == XML_ELEMENT_NODE)
	  {
	    if (strcmp (child->name, "item") != 0)
	      {
		*error = g_error_new (GREETER_PARSER_ERROR,
				      GREETER_PARSER_ERROR_BAD_SPEC,
				      "found tag %s when looking for item", child->name);
		return FALSE;
	      }
	    
	    type = xmlGetProp (child, "type");
	    if (!type)
	      {
		*error = g_error_new (GREETER_PARSER_ERROR,
				      GREETER_PARSER_ERROR_BAD_SPEC,
				      "items must specify their type");
		return FALSE;
	      }

	    if (strcmp (type, "svg") == 0)
	      item_type = GREETER_ITEM_TYPE_SVG;
	    else if (strcmp (type, "pixmap") == 0)
	      item_type = GREETER_ITEM_TYPE_PIXMAP;
	    else if (strcmp (type, "rect") == 0)
	      item_type = GREETER_ITEM_TYPE_RECT;
	    else if (strcmp (type, "label") == 0)
	      item_type = GREETER_ITEM_TYPE_LABEL;
	    else if (strcmp (type, "entry") == 0)
	      item_type = GREETER_ITEM_TYPE_ENTRY;
	    else
	      {
		*error = g_error_new (GREETER_PARSER_ERROR,
				      GREETER_PARSER_ERROR_BAD_SPEC,
				      "unknown item type %s", type);
		return FALSE;
	      }
	    
	    info = greeter_item_info_new (parent, item_type);
	    
	    parse_id (child, info);

	    switch (item_type)
	      {
	      case GREETER_ITEM_TYPE_SVG:
		res = parse_pixmap (child, TRUE, info, error);
		break;
	      case GREETER_ITEM_TYPE_PIXMAP:
		res = parse_pixmap (child, FALSE, info, error);
		break;
	      case GREETER_ITEM_TYPE_RECT:
		res = parse_rect (child, info, error);
		break;
	      case GREETER_ITEM_TYPE_LABEL:
		res = parse_label (child, info, error);
		break;
	      case GREETER_ITEM_TYPE_ENTRY:
		res = parse_entry (child, info, error);
		break;
	      default:
		*error = g_error_new (GREETER_PARSER_ERROR,
				      GREETER_PARSER_ERROR_BAD_SPEC,
				      "bad item type");
		res = FALSE;
	      }
	    
	    if (!res)
	      return FALSE;

	    items = g_list_prepend (items, info);
	    
	  }
	child = child->next;
      }

    *items_out = g_list_reverse (items);
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

GreeterItemInfo *
greeter_parse (char *file, char *datadir,
	       GnomeCanvas *canvas,
	       int width, int height, GError **error)
{
  GreeterItemInfo *root;
  xmlDocPtr doc;
  xmlNodePtr node;
  gboolean res;
  GList *items;

  file_search_path = datadir;
  
  if (!g_file_test (file, G_FILE_TEST_EXISTS))
    {
      if (error)
	*error = g_error_new (GREETER_PARSER_ERROR,
			      GREETER_PARSER_ERROR_NO_FILE,
			      "Can't open file %s", file);
      return NULL;
    }
  

  doc = xmlParseFile (file);
  if (doc == NULL)
    {
      if (error)
	*error = g_error_new (GREETER_PARSER_ERROR,
			      GREETER_PARSER_ERROR_BAD_XML,
			      "XML Parse error reading %s", file);
      return NULL;
    }
  
  node = xmlDocGetRootElement (doc);
  if (node == NULL)
    {
      if (error)
	*error = g_error_new (GREETER_PARSER_ERROR,
			      GREETER_PARSER_ERROR_BAD_XML,
			      "Can't find the xml root node in file %s", file);
      return NULL;
    }
  
  if (strcmp (node->name, "greeter") != 0)
    {
      if (error)
	*error = g_error_new (GREETER_PARSER_ERROR,
			      GREETER_PARSER_ERROR_WRONG_TYPE,
			      "The file %s has the wrong xml type", file);
      return NULL;
    }


  item_hash = g_hash_table_new_full ((GHashFunc)greeter_info_id_hash,
				     (GEqualFunc)greeter_info_id_equal,
				     NULL,
				     (GDestroyNotify)greeter_item_info_free);
  

  root = greeter_item_info_new (NULL, GREETER_ITEM_TYPE_RECT);
  res = parse_items (node, &items, root, error);
  if (!res)
    {
      greeter_item_info_free (root);

      return NULL;
    }

  root->fixed_children = items;
  
  root->x = 0;
  root->y = 0;
  root->x_type = GREETER_ITEM_POS_ABSOLUTE;
  root->y_type = GREETER_ITEM_POS_ABSOLUTE;

  root->width = width;
  root->height = height;
  root->width_type = GREETER_ITEM_SIZE_ABSOLUTE;
  root->width_type = GREETER_ITEM_SIZE_ABSOLUTE;

  root->group_item = gnome_canvas_root (canvas);
  
  return root;
}
