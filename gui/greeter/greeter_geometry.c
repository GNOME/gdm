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
#include <glib/gi18n.h>
#include <librsvg/rsvg.h>
#include "gdmcommon.h"
#include "gdmwm.h"
#include "greeter_geometry.h"
#include "greeter_canvas_item.h"

static void greeter_item_size_request   (GreeterItemInfo *item,
					 GtkRequisition  *requisition_out,
					 gint             parent_width,
					 gint             parent_height,
					 GnomeCanvas     *canvas);
static void greeter_size_allocate_fixed (GreeterItemInfo *fixed,
					 GList           *items,
					 GnomeCanvas     *canvas);
static void greeter_size_allocate_box   (GreeterItemInfo *box,
					 GList           *items,
					 GnomeCanvas     *canvas,
					 GtkAllocation   *allocation);
static void fixup_from_anchor           (GtkAllocation   *rect,
					 GtkAnchorType    anchor);
static void update_real_max_width	(GreeterItemInfo *info,
					 int              max_width);

static void
update_real_max_width (GreeterItemInfo *info, int max_width)
{
	if (info->data.text.real_max_width == 0 ||
	    info->data.text.real_max_width > max_width)
		info->data.text.real_max_width = max_width;
}

/* Position the item */
static void
greeter_item_size_allocate (GreeterItemInfo *item,
			    GtkAllocation   *allocation,
			    GnomeCanvas     *canvas)
{
  item->allocation = *allocation;

  if ( ! greeter_item_is_visible (item))
    return;

  if (item->item == NULL)
    greeter_item_create_canvas_item (item);

  if (item->fixed_children)
    greeter_size_allocate_fixed (item,
				 item->fixed_children,
				 canvas);

  if (item->box_children)
    greeter_size_allocate_box (item,
			       item->box_children,
			       canvas,
			       allocation);

}

static void
fixup_from_anchor (GtkAllocation *rect,
		   GtkAnchorType  anchor)
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
    default:
      break;
    }
}

/* Position the children of the parent given
 * the size */
static void
greeter_size_allocate_fixed (GreeterItemInfo *fixed,
			     GList           *items,
			     GnomeCanvas     *canvas)
{
  GList *l;
  GreeterItemInfo *child;
  GtkRequisition requisition;
  GtkAllocation child_allocation;

  l = items;
  while (l != NULL)
    {
      child = l->data;
      l = l->next;

      if ( ! greeter_item_is_visible (child))
        continue;

      greeter_item_size_request (child,
				 &requisition,
				 fixed->allocation.width,
				 fixed->allocation.height,
				 canvas);

      child_allocation.x = fixed->allocation.x;
      child_allocation.y = fixed->allocation.y;

      child_allocation.width = requisition.width;
      child_allocation.height = requisition.height;

      if (child->x_type == GREETER_ITEM_POS_ABSOLUTE)
	child_allocation.x += ( ! child->x_negative) ? child->x : fixed->allocation.width + child->x;
      else if (child->x_type == GREETER_ITEM_POS_RELATIVE)
	child_allocation.x += fixed->allocation.width * child->x / 100.0;

      if (child->y_type == GREETER_ITEM_POS_ABSOLUTE)
	child_allocation.y += ( ! child->y_negative) ? child->y : fixed->allocation.height + child->y;
      else if (child->y_type == GREETER_ITEM_POS_RELATIVE)
	child_allocation.y += fixed->allocation.height * child->y / 100.0;

      if (child->item_type != GREETER_ITEM_TYPE_LABEL)
	fixup_from_anchor (&child_allocation, child->anchor);

      greeter_item_size_allocate (child,
				  &child_allocation,
				  canvas);
    }
}

/* Position the children of the parent given
 * the size */
static void
greeter_size_allocate_box (GreeterItemInfo *box,
			   GList           *items,
			   GnomeCanvas     *canvas,
			   GtkAllocation   *allocation)
{
  GreeterItemInfo *child;
  GList *children;
  GtkAllocation child_allocation;
  GtkRequisition child_requisition;
  gint nvis_children;
  gint nexpand_children;
  gint child_major_size;
  gint major_size;
  gint extra;
  gint major;
  gint w, h;

  nvis_children = 0;
  nexpand_children = 0;

  children = items;
  while (children)
    {
      child = children->data;
      children = children->next;

      if (greeter_item_is_visible (child))
        {
          nvis_children += 1;
          if (child->expand)
	    nexpand_children += 1;
	}
    }

  if (nvis_children > 0)
    {
      if (box->box_orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  if (box->box_homogeneous)
	    {
	      major_size = (allocation->width -
			    box->box_x_padding * 2 -
			    (nvis_children - 1) * box->box_spacing);
	      extra = major_size / nvis_children;
	    }
	  else if (nexpand_children > 0)
	    {
	      major_size = (gint) allocation->width - (gint) box->requisition.width;
	      extra = major_size / nexpand_children;
	    }
	  else
	    {
	      major_size = 0;
	      extra = 0;
	    }

	  major = allocation->x + box->box_x_padding;
	}
      else
	{
	  if (box->box_homogeneous)
	    {
	      major_size = (allocation->height -
			    box->box_y_padding * 2 -
			    (nvis_children - 1) * box->box_spacing);
	      extra = major_size / nvis_children;
	    }
	  else if (nexpand_children > 0)
	    {
	      major_size = (gint) allocation->height - (gint) box->requisition.height;
	      extra = major_size / nexpand_children;
	    }
	  else
	    {
	      major_size = 0;
	      extra = 0;
	    }

	  major = allocation->y + box->box_y_padding;
	}

      children = items;
      while (children)
	{
	  child = children->data;
	  children = children->next;

          if ( ! greeter_item_is_visible (child))
	    continue;

	  if (box->box_homogeneous)
	    {
	      if (nvis_children == 1)
		child_major_size = major_size;
	      else
		child_major_size = extra;

	      major_size -= extra;

	      nvis_children -= 1;
	    }
	  else
	    {
	      greeter_item_size_request (child,
					 &child_requisition,
					 0, 0, canvas);

	      if (box->box_orientation == GTK_ORIENTATION_HORIZONTAL)
		child_major_size = child_requisition.width;
	      else
		child_major_size = child_requisition.height;

	      if (child->expand)
		{
		  if (nexpand_children == 1)
		    child_major_size += major_size;
		  else
		    child_major_size += extra;

		  nexpand_children -= 1;
		  major_size -= extra;
		}
	    }

	  /* Dirty the child requisition, since
	   * we now know the right parent size.
	   */
	  child->has_requisition = FALSE;
	  w = (box->box_orientation == GTK_ORIENTATION_HORIZONTAL) ? child_major_size : allocation->width - 2 * box->box_x_padding;
	  h = (box->box_orientation == GTK_ORIENTATION_HORIZONTAL) ? allocation->height - 2 * box->box_y_padding : child_major_size;

	  greeter_item_size_request (child,
				     &child_requisition,
				     w, h, canvas);

	  child_allocation.width = child_requisition.width;
	  child_allocation.height = child_requisition.height;

	  if (box->box_orientation == GTK_ORIENTATION_VERTICAL &&
	      child->item_type == GREETER_ITEM_TYPE_LABEL) {
		  update_real_max_width (child, allocation->width);
	  }

	  if (box->box_orientation == GTK_ORIENTATION_HORIZONTAL)
	    {
	      child_allocation.x = major;
	      child_allocation.y = allocation->y + box->box_y_padding;
	    }
	  else
	    {
	      child_allocation.x = allocation->x + box->box_x_padding;
	      child_allocation.y = major;
	    }

	  if (child->x_type == GREETER_ITEM_POS_ABSOLUTE)
	    child_allocation.x += ( ! child->x_negative) ? child->x : w + child->x;
	  else if (child->x_type == GREETER_ITEM_POS_RELATIVE)
	    child_allocation.x += w * child->x / 100.0;

	  if (child->y_type == GREETER_ITEM_POS_ABSOLUTE)
	    child_allocation.y += ( ! child->y_negative) ? child->y : h + child->y;
	  else if (child->y_type == GREETER_ITEM_POS_RELATIVE)
	    child_allocation.y += h * child->y / 100.0;

	  if (child->item_type != GREETER_ITEM_TYPE_LABEL)
	    fixup_from_anchor (&child_allocation, child->anchor);

	  if (child_allocation.x + child_allocation.width > allocation->x + allocation->width)
	      child_allocation.width -= (child_allocation.x + child_allocation.width
					 - allocation->x - allocation->width);

	  greeter_item_size_allocate (child,
				      &child_allocation,
				      canvas);

	  major += child_major_size + box->box_spacing;
	}
    }
}

static void
greeter_size_request_box (GreeterItemInfo *box,
			  GtkRequisition  *requisition,
			  GnomeCanvas     *canvas)
{

  GreeterItemInfo *child;
  GtkRequisition child_requisition;
  GList *children;
  gint nvis_children;

  requisition->width = 0;
  requisition->height = 0;

  nvis_children = 0;

  children = box->box_children;
  while (children)
    {
      child = children->data;
      children = children->next;

      if ( ! greeter_item_is_visible (child))
        continue;

      greeter_item_size_request (child,
				 &child_requisition,
				 0, 0, canvas);

      if (box->box_orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  if (box->box_homogeneous)
	    {
	      requisition->width = MAX (requisition->width,
					child_requisition.width);
	    }
	  else
	    {
	      requisition->width += child_requisition.width;
	    }

	  requisition->height = MAX (requisition->height, child_requisition.height);
	}
      else
	{
	  if (box->box_homogeneous)
	    {
	      requisition->height = MAX (requisition->height,
					 child_requisition.height);
	    }
	  else
	    {
	      requisition->height += child_requisition.height;
	    }

	  requisition->width = MAX (requisition->width, child_requisition.width);
	}

      nvis_children += 1;
    }

  if (nvis_children > 0)
    {

      if (box->box_orientation == GTK_ORIENTATION_HORIZONTAL)
	{
	  if (box->box_homogeneous)
	    requisition->width *= nvis_children;
	  requisition->width += (nvis_children - 1) * box->box_spacing;
	}
      else
	{
	  if (box->box_homogeneous)
	    requisition->height *= nvis_children;
	  requisition->height += (nvis_children - 1) * box->box_spacing;
	}
    }

  requisition->width += box->box_x_padding * 2;
  requisition->height += box->box_y_padding  * 2;

  requisition->width = MAX (requisition->width, box->box_min_width);
  requisition->height = MAX (requisition->height, box->box_min_height);
}

/* Calculate the requested minimum size of the item */
static void
greeter_item_size_request (GreeterItemInfo *item,
			   GtkRequisition  *requisition_out,
			   gint             parent_width,
			   gint             parent_height,
			   GnomeCanvas     *canvas)
{
  GtkRequisition *req;
  GtkRequisition box_requisition = {0, 0};
  int set_width = 0;
  int set_height = 0;

  if (item->has_requisition)
    {
      *requisition_out = item->requisition;
      return;
    }

  req = &item->requisition;

  req->width = 0;
  req->height = 0;

  if (item->width_type == GREETER_ITEM_SIZE_BOX ||
      item->height_type == GREETER_ITEM_SIZE_BOX)
    {
      greeter_size_request_box (item,
				&box_requisition,
				canvas);
    }

  switch (item->width_type)
    {
    case GREETER_ITEM_SIZE_ABSOLUTE:
      set_width = (item->width > 0) ? item->width : parent_width + item->width;
      break;
    case GREETER_ITEM_SIZE_RELATIVE:
      set_width = item->width*parent_width / 100.0;
      break;
    case GREETER_ITEM_SIZE_BOX:
      set_width = box_requisition.width;
      break;
    case GREETER_ITEM_SIZE_SCALE:
    case GREETER_ITEM_SIZE_UNSET:
      break;
    }

  switch (item->height_type)
    {
    case GREETER_ITEM_SIZE_ABSOLUTE:
      set_height = (item->height > 0) ? item->height : parent_height + item->height;
      break;
    case GREETER_ITEM_SIZE_RELATIVE:
      set_height = item->height*parent_height / 100.0;
      break;
    case GREETER_ITEM_SIZE_BOX:
      set_height = box_requisition.height;
      break;
    case GREETER_ITEM_SIZE_SCALE:
    case GREETER_ITEM_SIZE_UNSET:
      break;
    }

  if (item->item_type == GREETER_ITEM_TYPE_LABEL)
    {
      int width, height;
      char *text;
      int max_width = G_MAXINT;

      /* This is not the ugly hack you're looking for.
       * You can go about your business.
       * Move Along
       */
      text = gdm_common_expand_text (item->data.text.orig_text);

      if (set_width > 0)
	      max_width = set_width;

      if (item->data.text.max_width < max_width)
	      max_width = item->data.text.max_width;

      if (item->data.text.max_screen_percent_width/100.0 * gdm_wm_screen.width < max_width)
	      max_width = item->data.text.max_screen_percent_width/100.0 * gdm_wm_screen.width;

      greeter_canvas_item_break_set_string (item,
					    text,
					    TRUE /* markup */,
					    max_width,
					    &width,
					    &height,
					    canvas,
					    NULL /* real_item */);

      req->width = width;
      req->height = height;

      g_free (text);
    }

  if (item->item_type == GREETER_ITEM_TYPE_PIXMAP)
    {
      req->width = gdk_pixbuf_get_width (item->data.pixmap.pixbufs[0]);
      req->height = gdk_pixbuf_get_height (item->data.pixmap.pixbufs[0]);
    }

  if (item->item_type == GREETER_ITEM_TYPE_SVG)
    {
      GdkPixbuf *svg;

      svg = rsvg_pixbuf_from_file (item->data.pixmap.files[0], NULL);
      req->width = gdk_pixbuf_get_width (svg);
      req->height = gdk_pixbuf_get_height (svg);
      g_object_unref (svg);
    }

  if (item->item_type == GREETER_ITEM_TYPE_BUTTON)
    {
#define ITEM_BUTTON_MIN_RECOMMANDED_WIDTH_OFFSET 15
#define ITEM_BUTTON_MIN_RECOMMANDED_HEIGHT_OFFSET 10
      PangoLayout *layout;
      int pango_width, pango_height;
      int pix_width, pix_height;

      GtkWidget *dummy_w = gtk_button_new ();

      layout = gtk_widget_create_pango_layout (dummy_w, item->data.text.orig_text);

      pango_layout_get_size (layout, &pango_width, &pango_height);

      pix_height = PANGO_PIXELS (pango_height) + ITEM_BUTTON_MIN_RECOMMANDED_HEIGHT_OFFSET;
      pix_width = PANGO_PIXELS (pango_width) + ITEM_BUTTON_MIN_RECOMMANDED_WIDTH_OFFSET;

      if (pix_width > item->parent->box_min_width)
	req->width = pix_width;
      else
	req->width = item->parent->box_min_width;

      if (pix_height > item->parent->box_min_height)
	req->height = pix_height;
      else
	req->height = item->parent->box_min_height;
    }

  if (req->width > 0 && req->height > 0)
    {
      if (item->width_type == GREETER_ITEM_SIZE_SCALE && set_height > 0)
        set_width = (req->width * set_height) / req->height;
      else if (item->height_type == GREETER_ITEM_SIZE_SCALE && set_width > 0)
        set_height = (req->height * set_width) / req->width;
    }

  if (set_width > 0)
    req->width = set_width;
  if (set_height > 0)
    req->height = set_height;

  *requisition_out = item->requisition;
  item->has_requisition = TRUE;
}

void
greeter_layout (GreeterItemInfo *root_item,
		GnomeCanvas     *canvas)
{
  root_item->allocation.x = 0;
  root_item->allocation.y = 0;
  root_item->allocation.width = root_item->width;
  root_item->allocation.height = root_item->height;

  greeter_size_allocate_fixed (root_item,
			       root_item->fixed_children,
			       canvas);
}
