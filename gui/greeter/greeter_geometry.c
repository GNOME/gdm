#include <gtk/gtk.h>
#include "greeter_geometry.h"

static void  greeter_item_size_request (GreeterItemInfo *item,
					GtkRequisition  *requisition_out,
					gint             parent_width,
					gint             parent_height);


/* Position the item */
static void
greeter_item_size_allocate (GreeterItemInfo *item,
			    GtkAllocation   *allocation)
{
  
}

/* Position the children of the parent given
 * the size */
static void
greeter_size_allocate_fixed (GreeterItemInfo *fixed,
			     GList           *items,
			     GnomeCanvas     *canvas,
			     GtkAllocation   *allocation)
{
  
}


/* Position the children of the parent given
 * the size */
static void
greeter_size_allocate_box (GreeterItemInfo *box,
			   GList       *items,
			   GnomeCanvas *canvas,
			   GtkAllocation   *allocation)
{
  
}

static void
greeter_size_request_box (GreeterItemInfo *box,
			  GtkRequisition  *requisition)
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

      greeter_item_size_request (child,
				 &child_requisition,
				 0, 0);

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
}


/* Calculate the requested minimum size of the item */
static void
greeter_item_size_request (GreeterItemInfo *item,
			   GtkRequisition  *requisition_out,
			   gint             parent_width,
			   gint             parent_height)
{
  GtkRequisition *req;
  GtkRequisition box_requisition; 
  
  if (!item->has_requisition)
    {
      *requisition_out = item->requisition;
      return;
    }

  req = &item->requisition;
  
  req->width = 0;
  req->height = 0;

  if (item->item_type == GREETER_ITEM_TYPE_LABEL)
    {
      /* TODO: Calculate size of label */
      req->width = 100;
      req->height = 22;
    }

  if (item->item_type == GREETER_ITEM_TYPE_PIXMAP)
    {
      req->width = gdk_pixbuf_get_width (item->orig_pixbufs[0]);
      req->height = gdk_pixbuf_get_height (item->orig_pixbufs[0]);
    }
  
  if (item->width_type == GREETER_ITEM_SIZE_BOX ||
      item->height_type == GREETER_ITEM_SIZE_BOX)
    {
      greeter_size_request_box (item,
				&box_requisition);
    }

  switch (item->width_type)
    {
    case GREETER_ITEM_SIZE_ABSOLUTE:
      req->width = item->width;
      break;
    case GREETER_ITEM_SIZE_RELATIVE:
      req->width = item->width*parent_width / 100.0;
      break;
    case GREETER_ITEM_SIZE_BOX:
      req->width = box_requisition.width;
      break;
    case GREETER_ITEM_SIZE_UNSET:
      break;
    }

  switch (item->height_type)
    {
    case GREETER_ITEM_SIZE_ABSOLUTE:
      req->height = item->height;
      break;
    case GREETER_ITEM_SIZE_RELATIVE:
      req->height = item->height*parent_height / 100.0;
      break;
    case GREETER_ITEM_SIZE_BOX:
      req->height = box_requisition.height;
      break;
    case GREETER_ITEM_SIZE_UNSET:
      break;
    }

  *requisition_out = item->requisition;
}



void
greeter_layout (GreeterItemInfo *root_item,
		GnomeCanvas     *canvas,
		int              width,
		int              height)
{
  
}
