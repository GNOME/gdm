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
  GREETER_ITEM_STATE_MAX,
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemType {
  GREETER_ITEM_TYPE_RECT,
  GREETER_ITEM_TYPE_SVG,
  GREETER_ITEM_TYPE_PIXMAP,
  GREETER_ITEM_TYPE_LABEL,
  GREETER_ITEM_TYPE_ENTRY,
  GREETER_ITEM_TYPE_LIST,
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemSizeType {
  GREETER_ITEM_SIZE_UNSET,
  GREETER_ITEM_SIZE_ABSOLUTE,
  GREETER_ITEM_SIZE_RELATIVE,
  GREETER_ITEM_SIZE_BOX,
};

/* Make sure to adjust the bitfield in the structure if
   you make this larger */
enum _GreeterItemPosType {
  GREETER_ITEM_POS_UNSET,
  GREETER_ITEM_POS_ABSOLUTE,
  GREETER_ITEM_POS_RELATIVE,
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
  double x;
  double y;
  GreeterItemPosType x_type:2;
  GreeterItemPosType y_type:2;
  gboolean x_negative:1; /* needed for -0 */
  gboolean y_negative:1; /* needed for -0 */

  /* For packed items */
  gboolean expand:1;

  /* The item type */
  GreeterItemType item_type:4;

  GreeterItemShowModes show_modes:4;
  char *show_type; /* timed, system, config, chooser, halt, suspend, reboot */
  
  GreeterItemSizeType width_type:2;
  GreeterItemSizeType height_type:2;
  double width;
  double height;

  char *id;

  /* Button can propagate states and collect states from underlying items,
   * it should be a parent of this item */
  gboolean button;
  GreeterItemInfo *my_button;

  char *files[GREETER_ITEM_STATE_MAX];
  gdouble alphas[GREETER_ITEM_STATE_MAX];
  GdkPixbuf *pixbufs[GREETER_ITEM_STATE_MAX];
  guint32 tints[GREETER_ITEM_STATE_MAX];
  guint32 colors[GREETER_ITEM_STATE_MAX];

  guint8 have_color; /* this is a bitfield since these are
			true/false values */
  guint8 have_tint; /* this is a bitfield since these are
		       true/false values */
  guint8 have_state; /* this is a bitfield since these are
			true/false values */

  PangoFontDescription *fonts[GREETER_ITEM_STATE_MAX];
  char *orig_text;

  /* If this is a custom list, then these are the items
     to pick from */
  GList *list_items;

  /* Container data */
  GList *fixed_children;

  GtkOrientation box_orientation;
  gboolean box_homogeneous:1;
  double box_x_padding;
  double box_y_padding;
  double box_min_width;
  double box_min_height;
  double box_spacing;
  GList *box_children;
  
  /* Runtime state: */
  GreeterItemState state:2;
  GreeterItemState base_state:2;
  gboolean mouse_down:1;
  gboolean mouse_over:1;

  /* Canvas data: */
  GnomeCanvasItem *item;
  GnomeCanvasGroup *group_item;

  /* geometry handling: */
  gboolean has_requisition:1;
  GtkRequisition requisition;
  GtkAllocation allocation;
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
