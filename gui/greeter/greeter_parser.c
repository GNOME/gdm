/* GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <gtk/gtk.h>
#include <libxml/parser.h>
#include <string.h>
#include <stdlib.h>
#include <librsvg/rsvg.h>
#include <math.h>
#include <gdk/gdkx.h>
#include <locale.h>
#include <glib/gi18n.h>

#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "greeter_configuration.h"
#include "greeter_parser.h"
#include "greeter_events.h"

/* FIXME: hack */
extern GreeterItemInfo *welcome_string_info;

/* evil globals */
static char *file_search_path = NULL;
static GList *button_stack = NULL;

static GHashTable *pixbuf_hash = NULL;

GHashTable *item_hash = NULL;
GList *custom_items = NULL;

static gboolean parse_items (xmlNodePtr       node,
			     GList          **items_out,
			     GreeterItemInfo *parent,
			     GError         **error);

static GdkPixbuf *
load_pixbuf (const char *fname, GError **error)
{
  GdkPixbuf *pb;

  if (pixbuf_hash == NULL)
    pixbuf_hash = g_hash_table_new_full (g_str_hash,
					 g_str_equal,
					 g_free,
					 (GDestroyNotify)g_object_unref);
  pb = g_hash_table_lookup (pixbuf_hash, fname);
  if (pb != NULL)
    return g_object_ref (pb);

  pb = gdk_pixbuf_new_from_file (fname, error);
  if G_UNLIKELY (pb == NULL)
    return NULL;

  g_hash_table_insert (pixbuf_hash, g_strdup (fname), g_object_ref (pb));

  return pb;
}

GQuark
greeter_parser_error_quark (void)
{
  static GQuark quark = 0;
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
  
  prop = xmlGetProp (node, (const xmlChar *) "id");
  
  if (prop)
    {
      info->id = g_strdup ((char *) prop);
      g_hash_table_insert (item_hash, info, info);
      xmlFree (prop);
    }
}

static gboolean
parse_button (xmlNodePtr node,
	      GreeterItemInfo *info,
	      GError **error)
{
  xmlChar *prop;
  
  prop = xmlGetProp (node,(const xmlChar *) "button");
  if (prop)
    {
      if (strcmp ((char *) prop, "true") == 0)
	{
	  info->button = TRUE;
	}
      else if (strcmp ((char *) prop, "false") == 0)
	{
	  info->button = FALSE;
	}
      else
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "bad button spec %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  return TRUE;
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
  
  prop = xmlGetProp (node, (const xmlChar *) "anchor");
  if (prop)
    {
      if (strcmp ((char *) prop, "center") == 0)
	info->anchor = GTK_ANCHOR_CENTER;
      else if (strcmp ((char *) prop, "c") == 0)
	info->anchor = GTK_ANCHOR_CENTER;
      else if (strcmp ((char *) prop, "nw") == 0)
	info->anchor = GTK_ANCHOR_NW;
      else if (strcmp ((char *) prop, "n") == 0)
	info->anchor = GTK_ANCHOR_N;
      else if (strcmp ((char *) prop, "ne") == 0)
	info->anchor = GTK_ANCHOR_NE;
      else if (strcmp ((char *) prop, "w") == 0)
	info->anchor = GTK_ANCHOR_W;
      else if (strcmp ((char *) prop, "e") == 0)
	info->anchor = GTK_ANCHOR_E;
      else if (strcmp ((char *) prop, "sw") == 0)
	info->anchor = GTK_ANCHOR_SW;
      else if (strcmp ((char *) prop, "s") == 0)
	info->anchor = GTK_ANCHOR_S;
      else if (strcmp ((char *) prop, "se") == 0)
	info->anchor = GTK_ANCHOR_SE;
      else
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Unknown anchor type %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
   
  prop = xmlGetProp (node,(const xmlChar *) "x");
  if (prop)
    {
      info->x = g_ascii_strtod ((char *) prop, &p);

      if ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad position specifier %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}

      if (prop[0] == '-' || info->x < 0)
        info->x_negative = TRUE;
      else
        info->x_negative = FALSE;
      
      if (strchr ((char *) prop, '%') != NULL)
	info->x_type = GREETER_ITEM_POS_RELATIVE;
      else 
	info->x_type = GREETER_ITEM_POS_ABSOLUTE;
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "y");
  if (prop)
    {
      info->y = g_ascii_strtod ((char *) prop, &p);
      
      if ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad position specifier %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}

      if (prop[0] == '-' || info->y < 0)
        info->y_negative = TRUE;
      else
        info->y_negative = FALSE;
      
      if (strchr ((char *) prop, '%') != NULL)
	info->y_type = GREETER_ITEM_POS_RELATIVE;
      else 
	info->y_type = GREETER_ITEM_POS_ABSOLUTE;
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "width");
  if (prop)
    {
      if (strcmp ((char *) prop, "box") == 0)
	info->width_type = GREETER_ITEM_SIZE_BOX;
      else if (strcmp ((char *) prop, "scale") == 0)
       info->width_type = GREETER_ITEM_SIZE_SCALE;
      else
	{
	  info->width = g_ascii_strtod ((char *) prop, &p);

	  if ((char *)prop == p)
	    {
	      g_set_error (error,
			   GREETER_PARSER_ERROR,
			   GREETER_PARSER_ERROR_BAD_SPEC,
			   "Bad size specifier %s", prop);
	      xmlFree (prop);
	      return FALSE;
	    }
      
	  if (strchr ((char *) prop, '%') != NULL)
	    info->width_type = GREETER_ITEM_SIZE_RELATIVE;
	  else 
	    info->width_type = GREETER_ITEM_SIZE_ABSOLUTE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "height");
  if (prop)
    {
      if (strcmp ((char *) prop, "box") == 0)
	info->height_type = GREETER_ITEM_SIZE_BOX;
      else if (strcmp ((char *) prop, "scale") == 0)
       info->height_type = GREETER_ITEM_SIZE_SCALE;
      else
	{
	  info->height = g_ascii_strtod ((char *) prop, &p);
      
	  if ((char *)prop == p)
	    {
	      g_set_error (error,
			   GREETER_PARSER_ERROR,
			   GREETER_PARSER_ERROR_BAD_SPEC,
			   "Bad size specifier %s", prop);
	      xmlFree (prop);
	      return FALSE;
	    }
      
	  if (strchr ((char *) prop, '%') != NULL)
	    info->height_type = GREETER_ITEM_SIZE_RELATIVE;
	  else 
	    info->height_type = GREETER_ITEM_SIZE_ABSOLUTE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "expand");
  if (prop)
    {
      if (strcmp ((char *) prop, "true") == 0)
	{
	  info->expand = TRUE;
	}
      else if (strcmp ((char *) prop, "false") == 0)
	{
	  info->expand = FALSE;
	}
      else
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad expand spec %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }

  return TRUE;
}

static gboolean
parse_show (xmlNodePtr       node,
	    GreeterItemInfo *info,
	    GError         **error)
{
  xmlChar *prop;
  char **argv = NULL;
  int i;

  prop = xmlGetProp (node,(const xmlChar *) "type");
  if (prop != NULL)
    {
      g_free (info->show_type);
      info->show_type = g_strdup ((char *) prop);
      xmlFree (prop);
    }

  /* Note: subtype is deprecated, use type only */
  prop = xmlGetProp (node,(const xmlChar *) "subtype");
  if G_UNLIKELY (prop != NULL)
    {
      /* code for legacy uses of subtype only, are there any such
       * themes out there?  The Bluecurve was the one this was made
       * for and bluecurve is NOT using it. */
      if (info->show_type == NULL ||
	  strcmp (info->show_type, "system") == 0) {
	      g_free (info->show_type);
	      info->show_type = g_strdup ((char *) prop);
      }
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "modes");
  if (prop != NULL)
    {
      if (strcmp ((char *) prop, "everywhere") == 0)
        {
	  info->show_modes = GREETER_ITEM_SHOW_EVERYWHERE;
	  xmlFree (prop);
	  return TRUE;
	}
      else if (strcmp ((char *) prop, "nowhere") == 0)
        {
	  info->show_modes = GREETER_ITEM_SHOW_NOWHERE;
	  xmlFree (prop);
	  return TRUE;
	}

      argv = g_strsplit ((char *) prop, ",", 0);
      xmlFree (prop);
    }
  else
    {
      info->show_modes = GREETER_ITEM_SHOW_EVERYWHERE;
      return TRUE;
    }

  info->show_modes = GREETER_ITEM_SHOW_NOWHERE;

  if (argv != NULL)
    {
      for (i = 0; argv[i] != NULL; i++)
        {
          if (strcmp (argv[i], "console") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_CONSOLE;
	    }
          else if (strcmp (argv[i], "console-fixed") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_CONSOLE_FIXED;
	    }
          else if (strcmp (argv[i], "console-flexi") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_CONSOLE_FLEXI;
	    }
          else if (strcmp (argv[i], "remote-flexi") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_REMOTE_FLEXI;
	    }
          else if (strcmp (argv[i], "flexi") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_FLEXI;
	    }
          else if (strcmp (argv[i], "remote") == 0)
            {
	      info->show_modes |= GREETER_ITEM_SHOW_REMOTE;
	    }
	}
      g_strfreev (argv);
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
  
  prop = xmlGetProp (node,(const xmlChar *) "orientation");
  if (prop)
    {
      if (strcmp ((char *) prop, "horizontal") == 0)
	{
	  info->box_orientation = GTK_ORIENTATION_HORIZONTAL;
	}
      else if (strcmp ((char *) prop, "vertical") == 0)
	{
	  info->box_orientation = GTK_ORIENTATION_VERTICAL;
	}
      else
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad orientation %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "homogeneous");
  if (prop)
    {
      if (strcmp ((char *) prop, "true") == 0)
	{
	  info->box_homogeneous = TRUE;
	}
      else if (strcmp ((char *) prop, "false") == 0)
	{
	  info->box_homogeneous = FALSE;
	}
      else
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad homogenous spec %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      
      xmlFree (prop);
    }


  prop = xmlGetProp (node,(const xmlChar *) "xpadding");
  if (prop)
    {
      info->box_x_padding = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad padding specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "ypadding");
  if (prop)
    {
      info->box_y_padding = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad padding specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "min-width");
  if (prop)
    {
      info->box_min_width = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad min-width specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "min-height");
  if (prop)
    {
      info->box_min_height = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad min-height specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "spacing");
  if (prop)
    {
      info->box_spacing = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad spacing specification %s", prop);
	  xmlFree (prop);
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
  if G_UNLIKELY (str[0] != '#')
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "colors must start with #, %s is an invalid color", str);
      return FALSE;
    }
  if G_UNLIKELY (strlen (str) != 7)
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "Colors must be on the format #xxxxxx, %s is an invalid color", str);
      return FALSE;
    }

  col = 0;

  for (i = 0; i < 6; i++)
    col = (col << 4)  | g_ascii_xdigit_value (str[i+1]);

  *col_out = col;

  return TRUE;
}

static gboolean
parse_state_file_pixmap (xmlNodePtr node,
			 GreeterItemInfo  *info,
			 GreeterItemState state,
			 GError         **error)
{
  xmlChar *prop;
  char *p;

  info->have_state |= (1<<state);
  
  prop = xmlGetProp (node,(const xmlChar *) "file");
  if (prop)
    {
      if (g_path_is_absolute ((char *) prop))
	info->data.pixmap.files[state] = g_strdup ((char *) prop);
      else
	info->data.pixmap.files[state] = g_build_filename (file_search_path,
					       (char *) prop,
					       NULL);
	
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "tint");
  if (prop)
    {
      if (!parse_color ((char *) prop, &info->data.pixmap.tints[state], error))
	return FALSE;
      info->data.pixmap.have_tint |= (1<<state);
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "alpha");
  if (prop)
    {
      double alpha = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad alpha specifier format %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);

      if (alpha >= 1.0)
        info->data.pixmap.alphas[state] = 0xff;
      else if (alpha < 0)
        info->data.pixmap.alphas[state] = 0;
      else
        info->data.pixmap.alphas[state] = floor (alpha * 0xff);
    }
  
  return TRUE;
}

static gboolean
parse_state_color_rect (xmlNodePtr node,
			GreeterItemInfo  *info,
			GreeterItemState state,
			GError         **error)
{
  xmlChar *prop;
  char *p;

  info->have_state |= (1<<state);
  
  prop = xmlGetProp (node,(const xmlChar *) "color");
  if (prop)
    {
      if G_UNLIKELY (!parse_color ((char *) prop, &info->data.rect.colors[state], error))
	return FALSE;
      info->data.rect.have_color |= (1<<state);
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "alpha");
  if (prop)
    {
      double alpha = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad alpha specifier format %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);

      if (alpha >= 1.0)
        info->data.rect.alphas[state] = 0xff;
      else if (alpha < 0)
        info->data.rect.alphas[state] = 0;
      else
        info->data.rect.alphas[state] = floor (alpha * 0xff);
    }
  
  return TRUE;
}

static gboolean
parse_color_list (xmlNodePtr node,
		      GreeterItemInfo  *info,
		      GError         **error)
{
  xmlChar *prop;
  guint32 color;

  prop = xmlGetProp (node,(const xmlChar *) "iconcolor");
  if (prop)
    {
      if G_UNLIKELY (!parse_color ((char *) prop, &color, error)) {
        info->data.list.icon_color = NULL;
	return FALSE;
      } else {
        info->data.list.icon_color = g_strdup ((char *) prop);
      }

      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "labelcolor");
  if (prop)
    {
      if G_UNLIKELY (!parse_color ((char *) prop, &color, error)) {
        info->data.list.label_color = NULL;
	return FALSE;
      } else {
        info->data.list.label_color = g_strdup ((char *) prop);
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
      if (strcmp ((char *) child->name, "normal") == 0)
	{
	  if G_UNLIKELY (!parse_state_file_pixmap (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "prelight") == 0)
	{
	  if G_UNLIKELY (!parse_state_file_pixmap (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "active") == 0)
	{
	  if G_UNLIKELY (!parse_state_file_pixmap (child, info, GREETER_ITEM_STATE_ACTIVE, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "pos") == 0)
	{
	  if G_UNLIKELY (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "fixed") == 0)
	{
	  if G_UNLIKELY (!parse_fixed (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "box") == 0)
	{
	  if G_UNLIKELY (!parse_box (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "show") == 0)
	{
	  if G_UNLIKELY (!parse_show (child, info, error))
	    return FALSE;
	}
      
      child = child->next;
    }

  if G_UNLIKELY (!info->data.pixmap.files[GREETER_ITEM_STATE_NORMAL])
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "No filename specified for normal state");
      return FALSE;
    }
  
  if (!svg)
    {
      for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
	{
	  if (info->data.pixmap.files[i] != NULL)
	    {
	      info->data.pixmap.pixbufs[i] = load_pixbuf (info->data.pixmap.files[i], error);
	      
	      if G_UNLIKELY (info->data.pixmap.pixbufs[i] == NULL)
		return FALSE;
	    }
	  else
	    info->data.pixmap.pixbufs[i] = NULL;
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
      if (strcmp ((char *) child->name, "normal") == 0)
	{
	  if G_UNLIKELY (!parse_state_color_rect (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "prelight") == 0)
	{
	  if G_UNLIKELY (!parse_state_color_rect (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "active") == 0)
	{
	  if G_UNLIKELY (!parse_state_color_rect (child, info, GREETER_ITEM_STATE_ACTIVE, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "pos") == 0)
	{
	  if G_UNLIKELY (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "fixed") == 0)
	{
	  if G_UNLIKELY (!parse_fixed (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "box") == 0)
	{
	  if G_UNLIKELY (!parse_box (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "show") == 0)
	{
	  if G_UNLIKELY (!parse_show (child, info, error))
	    return FALSE;
	}
      
      child = child->next;
    }

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if ( ! (info->data.rect.have_color & (1<<i)))
	continue;
      
      info->data.rect.colors[i] = (info->data.rect.colors[i] << 8) | (guint) info->data.rect.alphas[i];
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

  info->have_state |= (1<<state);

  prop = xmlGetProp (node,(const xmlChar *) "font");
  if (prop)
    {
      info->data.text.fonts[state] = pango_font_description_from_string ((char *) prop);
      if G_UNLIKELY (info->data.text.fonts[state] == NULL)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad font specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }
  
  prop = xmlGetProp (node,(const xmlChar *) "color");
  if (prop)
    {
      if G_UNLIKELY (!parse_color ((char *) prop, &info->data.text.colors[state], error))
	return FALSE;
      info->data.text.have_color |= (1<<state);
      xmlFree (prop);
   }

  prop = xmlGetProp (node,(const xmlChar *) "alpha");
  if (prop)
    {
      double alpha = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad alpha specifier format %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);

      if (alpha >= 1.0)
        info->data.rect.alphas[state] = 0xff;
      else if (alpha < 0)
        info->data.rect.alphas[state] = 0;
      else
        info->data.rect.alphas[state] = floor (alpha * 0xff);
    }
  
  return TRUE;
}

static gint
is_current_locale (const char *lang)
{
  const GList *l = ve_i18n_get_language_list ("LC_MESSAGES");
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
  xmlChar *text;
  xmlChar *prop;
  gint score;
  
  prop = xmlNodeGetLang (node);
  if (prop)
    {
      score = is_current_locale ((char *) prop);
      xmlFree (prop);
    } else
      score = 999;

  if (score >= *translation_score)
    return TRUE;
  
  text = xmlNodeGetContent (node);
  if (text == NULL)
    {
      /* This is empty text */
      *translation_score = score;
      if (*translated_text)
        g_free (*translated_text);
      *translated_text = g_strdup ("");
  
      return TRUE;
    }

  *translation_score = score;
  if (*translated_text)
    g_free (*translated_text);
  *translated_text = g_strdup ((char *) text);
  
  xmlFree (text);
  
  return TRUE;
}

/* We pass the same arguments as to translated text, since we'll override it
 * with translation score */
static gboolean
parse_stock (xmlNodePtr node,
	     GreeterItemInfo *info,
	     char     **translated_text,
	     gint      *translation_score,
	     GError   **error)
{
  xmlChar *prop;
  
  prop = xmlGetProp (node,(const xmlChar *) "type");
  if (prop)
    {
      if (g_ascii_strcasecmp ((char *) prop, "language") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Language"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "session") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Session"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "system") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Actions"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "disconnect") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("D_isconnect"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "quit") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Quit"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "halt") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Shut _Down"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "suspend") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Sus_pend"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "reboot") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Restart"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "chooser") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Remote Login via _XDMCP"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "config") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Confi_gure"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "options") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Op_tions"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "caps-lock-warning") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Caps Lock is on."));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "timed-label") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("User %u will login in %t"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "welcome-label") == 0)
        {
	  /* FIXME: hack */
	  welcome_string_info = info;

	  g_free (*translated_text);
	  *translated_text = gdm_common_get_welcomemsg ();
	}
      /* FIXME: is this actually needed? */
      else if (g_ascii_strcasecmp ((char *) prop, "username-label") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("Username:"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "ok") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_OK"));
	}
      else if (g_ascii_strcasecmp ((char *) prop, "cancel") == 0)
        {
	  g_free (*translated_text);
	  *translated_text = g_strdup (_("_Cancel"));
	}
      else
        {
          g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad stock label type");
	  xmlFree (prop);
	  return FALSE;
	}

      /* This is the very very very best "translation" */
      *translation_score = -1;

      xmlFree (prop);

      return TRUE;
    }
  else
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "Stock type not specified");
      return FALSE;
    }
}

static void
do_font_size_reduction (GreeterItemInfo *info)
{
  double size_reduction = 1.0;
  int i;

  if (gdm_wm_screen.width <= 800 &&
      gdm_wm_screen.width > 640)
    size_reduction = PANGO_SCALE_SMALL;
  else if (gdm_wm_screen.width <= 640)
    size_reduction = PANGO_SCALE_X_SMALL;

  if (size_reduction < 0.99)
    {
      for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
        {
          if (info->data.text.fonts[i] != NULL)
	    {
	      int old_size = pango_font_description_get_size (info->data.text.fonts[i]);
	      pango_font_description_set_size (info->data.text.fonts[i], old_size * size_reduction);
	    }
	}
    }
}


static gboolean
parse_label_pos_extras (xmlNodePtr       node,
			GreeterItemInfo *info,
			GError         **error)
{
  xmlChar *prop;
  char *p;
  
  prop = xmlGetProp (node,(const xmlChar *) "max-width");
  if (prop)
    {
      info->data.text.max_width = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad max-width specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }

  prop = xmlGetProp (node,(const xmlChar *) "max-screen-percent-width");
  if (prop)
    {
      info->data.text.max_screen_percent_width = g_ascii_strtod ((char *) prop, &p);
      
      if G_UNLIKELY ((char *)prop == p)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Bad max-screen-percent-width specification %s", prop);
	  xmlFree (prop);
	  return FALSE;
	}
      xmlFree (prop);
    }

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
      if (strcmp ((char *) child->name, "normal") == 0)
	{
	  if G_UNLIKELY (!parse_state_text (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "prelight") == 0)
	{
	  if G_UNLIKELY (!parse_state_text (child, info, GREETER_ITEM_STATE_PRELIGHT, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "active") == 0)
	{
	  if G_UNLIKELY (!parse_state_text (child, info, GREETER_ITEM_STATE_ACTIVE, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "pos") == 0)
	{
	  if G_UNLIKELY (!parse_pos (child, info, error))
	    return FALSE;
	  if G_UNLIKELY (!parse_label_pos_extras (child, info, error))
	    return FALSE;
	}
      else if (child->type == XML_ELEMENT_NODE &&
	       strcmp ((char *) child->name, "text") == 0)
	{
	  if G_UNLIKELY (!parse_translated_text (child, &translated_text, &translation_score, error))
	    return FALSE;
	}
      else if (child->type == XML_ELEMENT_NODE &&
	       strcmp ((char *) child->name, "stock") == 0)
	{
	  if G_UNLIKELY (!parse_stock (child, info, &translated_text, &translation_score, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "show") == 0)
	{
	  if G_UNLIKELY (!parse_show (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "fixed") == 0 ||
	       strcmp ((char *) child->name, "box") == 0)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Label items cannot have children");
	  return FALSE;
	}
	  
      child = child->next;
    }

  if (translated_text == NULL)
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "A label must specify the text attribute");
      return FALSE;
    }
  /* FIXME: evil hack to use internally translated strings */
  if (translation_score == 999 &&
      ! ve_string_empty (translated_text))
    {
      char *foo = g_strdup (_(translated_text));
      g_free (translated_text);
      translated_text = foo;
    }

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if ( ! (info->data.text.have_color & (1<<i)))
	continue;
      
      info->data.text.colors[i] = (info->data.text.colors[i] << 8) | (guint) info->data.text.alphas[i];
    }
  
  if (info->data.text.fonts[GREETER_ITEM_STATE_NORMAL] == NULL)
    info->data.text.fonts[GREETER_ITEM_STATE_NORMAL] = pango_font_description_from_string ("Sans");

  do_font_size_reduction (info);

  info->data.text.orig_text = translated_text;
  
  return TRUE;
}

static gboolean
parse_listitem (xmlNodePtr        node,
		GreeterItemInfo  *info,
		GError         **error)
{
  xmlNodePtr child;
  char *translated_text = NULL;
  gint translation_score = 1000;
  xmlChar *prop;
  GreeterItemListItem *li;
  
  prop = xmlGetProp (node,(const xmlChar *) "id");
  
  if G_LIKELY (prop)
    {
      li = g_new0 (GreeterItemListItem, 1);
      li->id = g_strdup ((char *) prop);
      xmlFree (prop);
    }
  else
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "Listitem id not specified");
      return FALSE;
    }

  child = node->children;
  while (child)
    {
      if (child->type == XML_ELEMENT_NODE &&
	  strcmp ((char *) child->name, "text") == 0)
	{
	  if G_UNLIKELY ( ! parse_translated_text (child, &translated_text, &translation_score, error))
	    {
              g_free (li->id);
              g_free (li);
	      return FALSE;
	    }
	}
    
      child = child->next;
    }

  if G_UNLIKELY (translated_text == NULL)
    {
      g_free (li->id);
      g_free (li);
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "A list item must specify the text attribute");
      return FALSE;
    }
  li->text = translated_text;

  info->data.list.items = g_list_append (info->data.list.items, li);

  return TRUE;
}

static gboolean
parse_list (xmlNodePtr        node,
	     GreeterItemInfo  *info,
	     GError         **error)
{
  xmlNodePtr child;

  child = node->children;
  while (child)
    {
      if (strcmp ((char *) child->name, "color") == 0)
	{
	  if G_UNLIKELY (!parse_color_list (child, info, error))
	    return FALSE;
	}
      if (strcmp ((char *) child->name, "pos") == 0)
	{
	  if G_UNLIKELY (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "show") == 0)
	{
	  if G_UNLIKELY (!parse_show (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "listitem") == 0)
	{
	  if G_UNLIKELY ( ! parse_listitem (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "fixed") == 0 ||
	       strcmp ((char *) child->name, "box") == 0)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "List items cannot have children");
	  return FALSE;
	}
    
      child = child->next;
    }

  if (info->data.list.items != NULL) {
    if G_UNLIKELY (strcmp (info->id, "userlist") == 0) {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_SPEC,
		   "List of id userlist cannot have custom list items");
      return FALSE;
    }
    custom_items = g_list_append (custom_items, info);
  }

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
      if (strcmp ((char *) child->name, "normal") == 0)
	{
	  if G_UNLIKELY (!parse_state_text (child, info, GREETER_ITEM_STATE_NORMAL, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "pos") == 0)
	{
	  if G_UNLIKELY (!parse_pos (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "show") == 0)
	{
	  if G_UNLIKELY (!parse_show (child, info, error))
	    return FALSE;
	}
      else if (strcmp ((char *) child->name, "fixed") == 0 ||
	       strcmp ((char *) child->name, "box") == 0)
	{
	  g_set_error (error,
		       GREETER_PARSER_ERROR,
		       GREETER_PARSER_ERROR_BAD_SPEC,
		       "Entry items cannot have children");
	  return FALSE;
	}
    
      child = child->next;
    }

  do_font_size_reduction (info);

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

    *items_out = NULL;
    
    items = NULL;
    
    child = node->children;
    while (child)
      {
	if (child->type == XML_ELEMENT_NODE)
	  {
	    if G_UNLIKELY (strcmp ((char *) child->name, "item") != 0)
	      {
		g_set_error (error,
			     GREETER_PARSER_ERROR,
			     GREETER_PARSER_ERROR_BAD_SPEC,
			     "Found tag %s when looking for item", child->name);
		return FALSE;
	      }
	    
	    type = xmlGetProp (child, (const xmlChar *) "type");
	    if G_UNLIKELY (!type)
	      {
		g_set_error (error,
			     GREETER_PARSER_ERROR,
			     GREETER_PARSER_ERROR_BAD_SPEC,
			     "Items must specify their type");
		return FALSE;
	      }

	    if (strcmp ((char *) type, "svg") == 0)
	      item_type = GREETER_ITEM_TYPE_SVG;
	    else if (strcmp ((char *) type, "pixmap") == 0)
	      item_type = GREETER_ITEM_TYPE_PIXMAP;
	    else if (strcmp ((char *) type, "rect") == 0)
	      item_type = GREETER_ITEM_TYPE_RECT;
	    else if (strcmp ((char *) type, "label") == 0)
	      item_type = GREETER_ITEM_TYPE_LABEL;
	    else if (strcmp ((char *) type, "entry") == 0)
	      item_type = GREETER_ITEM_TYPE_ENTRY;
	    else if (strcmp ((char *) type, "list") == 0)
	      item_type = GREETER_ITEM_TYPE_LIST;
	    else
	      {
		g_set_error (error,
			     GREETER_PARSER_ERROR,
			     GREETER_PARSER_ERROR_BAD_SPEC,
			     "Unknown item type %s", type);
		xmlFree (type);
		return FALSE;
	      }
	    
	    info = greeter_item_info_new (parent, item_type);
	    
	    parse_id (child, info);
	    if G_UNLIKELY ( ! parse_button (child, info, error))
	      return FALSE;

	    if (button_stack != NULL)
	      info->my_button = button_stack->data;
	    if (info->button)
	      button_stack = g_list_prepend (button_stack, info);

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
	      case GREETER_ITEM_TYPE_LIST:
		res = parse_list (child, info, error);
		break;
	      default:
		g_set_error (error,
			     GREETER_PARSER_ERROR,
			     GREETER_PARSER_ERROR_BAD_SPEC,
			     "Bad item type");
		res = FALSE;
	      }

	    if (info->button)
	      button_stack = g_list_remove (button_stack, info);
	    
	    if G_UNLIKELY (!res)
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
greeter_parse (const char *file, const char *datadir,
	       GnomeCanvas *canvas,
	       int width, int height, GError **error)
{
  GreeterItemInfo *root;
  xmlDocPtr doc;
  xmlNodePtr node;
  xmlChar *prop;
  gboolean res;
  GList *items;
  
  /* FIXME: EVIL! GLOBAL! */
  g_free (file_search_path);
  file_search_path = g_strdup (datadir);
  
  if G_UNLIKELY (!g_file_test (file, G_FILE_TEST_EXISTS))
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_NO_FILE,
		   "Can't open file %s", file);
      return NULL;
    }
  

  doc = xmlParseFile (file);
  if G_UNLIKELY (doc == NULL)
    {
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_XML,
		   "XML Parse error reading %s", file);
      return NULL;
    }
  
  node = xmlDocGetRootElement (doc);
  if G_UNLIKELY (node == NULL)
    {
      xmlFreeDoc (doc);
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_BAD_XML,
		   "Can't find the xml root node in file %s", file);
      return NULL;
    }
  
  if G_UNLIKELY (strcmp ((char *) node->name, "greeter") != 0)
    {
      xmlFreeDoc (doc);
      g_set_error (error,
		   GREETER_PARSER_ERROR,
		   GREETER_PARSER_ERROR_WRONG_TYPE,
		   "The file %s has the wrong xml type", file);
      return NULL;
    }

  /*
   * The gtk-theme property specifies a theme specific gtk-theme to use
   */
  prop = xmlGetProp (node, (const xmlChar *) "gtk-theme");
  if (prop)
    {
      gchar *theme_dir;

      /*
       * It might be nice if we allowed this property to also supply a gtkrc file
       * that could be included in the theme.  Perhaps we should check first in
       * the theme directory for a gtkrc file by the provided name and use that
       * if found.
       */
      theme_dir = g_strdup_printf ("%s/%s", gtk_rc_get_theme_dir (), (char *) prop);
      if (g_file_test (theme_dir, G_FILE_TEST_IS_DIR))
         gdm_set_theme ((char *) prop);

      xmlFree (prop);
    }

  item_hash = g_hash_table_new ((GHashFunc)greeter_info_id_hash,
				(GEqualFunc)greeter_info_id_equal);
  

  root = greeter_item_info_new (NULL, GREETER_ITEM_TYPE_RECT);
  res = parse_items (node, &items, root, error);

  /* Now we can whack the hash, we don't want to keep cached
     pixbufs around anymore */
  g_hash_table_destroy (pixbuf_hash);
  pixbuf_hash = NULL;

  if G_UNLIKELY (!res)
    {
      welcome_string_info = NULL;

      g_hash_table_destroy (item_hash);
      item_hash = NULL;
      g_list_free (custom_items);
      custom_items = NULL;

      g_list_free (button_stack);
      button_stack = NULL;

      g_list_foreach (items, (GFunc) greeter_item_info_free, NULL);
      g_list_free (items);
      items = NULL;

      greeter_item_info_free (root);

      xmlFreeDoc (doc);

      return NULL;
    }

  xmlFreeDoc (doc);

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

const GList *
greeter_custom_items (void)
{
  return custom_items;
}
