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

#ifndef GREETER_ITEM_H
#define GREETER_ITEM_H

#include <libgnomecanvas/libgnomecanvas.h>

typedef struct _GreeterItemInfo GreeterItemInfo;
typedef struct _GreeterItemListItem GreeterItemListItem;
typedef enum _GreeterItemState GreeterItemState;
typedef enum _GreeterItemType GreeterItemType;
typedef enum _GreeterItemSizeType GreeterItemSizeType;
typedef enum _GreeterItemPosType GreeterItemPosType;
typedef enum _GreeterItemShowModes GreeterItemShowModes;

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemState {
  GREETER_ITEM_STATE_NORMAL,
  GREETER_ITEM_STATE_PRELIGHT,
  GREETER_ITEM_STATE_ACTIVE,
  GREETER_ITEM_STATE_MAX
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemType {
  GREETER_ITEM_TYPE_RECT,
  GREETER_ITEM_TYPE_SVG,
  GREETER_ITEM_TYPE_PIXMAP,
  GREETER_ITEM_TYPE_LABEL,
  GREETER_ITEM_TYPE_ENTRY,
  GREETER_ITEM_TYPE_LIST
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemSizeType {
  GREETER_ITEM_SIZE_UNSET,
  GREETER_ITEM_SIZE_ABSOLUTE,
  GREETER_ITEM_SIZE_RELATIVE,
  GREETER_ITEM_SIZE_BOX
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemPosType {
  GREETER_ITEM_POS_UNSET,
  GREETER_ITEM_POS_ABSOLUTE,
  GREETER_ITEM_POS_RELATIVE
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemShowModes {
  GREETER_ITEM_SHOW_EVERYWHERE = 0xf,
  GREETER_ITEM_SHOW_NOWHERE = 0,
  GREETER_ITEM_SHOW_CONSOLE_FIXED = 1<<0,
  GREETER_ITEM_SHOW_CONSOLE = (1<<0) | (1<<1),
  GREETER_ITEM_SHOW_CONSOLE_FLEXI = 1<<1,
  GREETER_ITEM_SHOW_REMOTE_FLEXI = 1<<2,
  GREETER_ITEM_SHOW_FLEXI = (1<<1) | (1<<2),
  GREETER_ITEM_SHOW_REMOTE = 1<<3
};

struct _GreeterItemInfo {
  GreeterItemInfo *parent;
  
  GtkAnchorType anchor;
  float x;
  float y;
  float width;
  float height;
  GreeterItemPosType x_type:2;
  GreeterItemPosType y_type:2;
  GreeterItemSizeType width_type:2;
  GreeterItemSizeType height_type:2;
  guint x_negative:1; /* needed for -0 */
  guint y_negative:1; /* needed for -0 */

  /* For packed items */
  guint expand:1;

  /* The item type */
  GreeterItemType item_type:4;

  GreeterItemShowModes show_modes:4;

  /* Runtime state: */
  GreeterItemState state:2;
  GreeterItemState base_state:2;
  guint mouse_down:1;
  guint mouse_over:1;

  /* box flags */
  guint box_homogeneous:1;

  /* is a button (see my_button comment) */
  guint button:1;

  /* geometry handling: */
  guint has_requisition:1;
  GtkRequisition requisition;
  GtkAllocation allocation;

  /* Button can propagate states and collect states from underlying items,
   * it should be a parent of this item */
  GreeterItemInfo *my_button;

  char *show_type; /* timed, system, config, chooser, halt, suspend, reboot */

  char *id;

  GList *box_children;
  GtkOrientation box_orientation;
  guint16 box_x_padding;
  guint16 box_y_padding;
  guint16 box_min_width;
  guint16 box_min_height;
  guint16 box_spacing;

  /* Container data */
  GList *fixed_children;

  union {
	  /* Note: we want to have alphas, colors and have_color coincide for all types
	     that have it */
#define GREETER_ITEM_TYPE_IS_TEXT(info) ((info)->item_type == GREETER_ITEM_TYPE_LABEL || (info)->item_type == GREETER_ITEM_TYPE_ENTRY)
	  struct {
		  guint8 alphas[GREETER_ITEM_STATE_MAX];
		  guint32 colors[GREETER_ITEM_STATE_MAX];

		  guint8 have_color; /* this is a bitfield since these are
					true/false values */

		  PangoFontDescription *fonts[GREETER_ITEM_STATE_MAX];
		  char *orig_text;
		  guint16 max_width;
		  guint8 max_screen_percent_width;
		  guint16 real_max_width;
	  } text; /* text and entry (entry only uses fonts) */

#define GREETER_ITEM_TYPE_IS_PIXMAP(info) ((info)->item_type == GREETER_ITEM_TYPE_PIXMAP || (info)->item_type == GREETER_ITEM_TYPE_SVG)
	  struct {
		  guint8 alphas[GREETER_ITEM_STATE_MAX];
		  guint32 tints[GREETER_ITEM_STATE_MAX];
		  guint8 have_tint; /* this is a bitfield since these are
				       true/false values */

		  char *files[GREETER_ITEM_STATE_MAX];
		  GdkPixbuf *pixbufs[GREETER_ITEM_STATE_MAX];
	  } pixmap;

#define GREETER_ITEM_TYPE_IS_LIST(info) ((info)->item_type == GREETER_ITEM_TYPE_LIST)
	  struct {
		  /* If this is a custom list, then these are the items
		     to pick from */
		  GList *items;
	  } list;

#define GREETER_ITEM_TYPE_IS_RECT(info) ((info)->item_type == GREETER_ITEM_TYPE_RECT)
	  struct {
		  guint8 alphas[GREETER_ITEM_STATE_MAX];
		  guint32 colors[GREETER_ITEM_STATE_MAX];

		  guint8 have_color; /* this is a bitfield since these are
					true/false values */
	  } rect;
  } data;

  guint8 have_state; /* this is a bitfield since these are
			true/false values */

  /* Canvas data: */
  GnomeCanvasItem *item;
  GnomeCanvasGroup *group_item;
};

struct _GreeterItemListItem {
	char *id;
	char *text;
};

gint greeter_item_event_handler (GnomeCanvasItem *item,
				 GdkEvent        *event,
				 gpointer         data);

GreeterItemInfo *greeter_item_info_new (GreeterItemInfo *parent,
					GreeterItemType  type);
void greeter_item_info_free (GreeterItemInfo *info);

char *greeter_item_expand_text (const char *text);

void greeter_item_update_text (GreeterItemInfo *info);

gboolean greeter_item_is_visible (GreeterItemInfo *info);

#endif /* GREETER_ITEM_H */
