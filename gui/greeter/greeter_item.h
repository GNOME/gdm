#ifndef GREETER_ITEM_H
#define GREETER_ITEM_H

#include <libgnomecanvas/libgnomecanvas.h>

typedef struct _GreeterItemInfo GreeterItemInfo;
typedef enum _GreeterItemState GreeterItemState;
typedef enum _GreeterItemType GreeterItemType;
typedef enum _GreeterItemSizeType GreeterItemSizeType;
typedef enum _GreeterItemPosType GreeterItemPosType;
typedef enum _GreeterItemShowModes GreeterItemShowModes;

enum _GreeterItemState {
  GREETER_ITEM_STATE_NORMAL,
  GREETER_ITEM_STATE_PRELIGHT,
  GREETER_ITEM_STATE_ACTIVE,
  GREETER_ITEM_STATE_MAX,
};

enum _GreeterItemType {
  GREETER_ITEM_TYPE_RECT,
  GREETER_ITEM_TYPE_SVG,
  GREETER_ITEM_TYPE_PIXMAP,
  GREETER_ITEM_TYPE_LABEL,
  GREETER_ITEM_TYPE_ENTRY,
};

enum _GreeterItemSizeType {
  GREETER_ITEM_SIZE_UNSET,
  GREETER_ITEM_SIZE_ABSOLUTE,
  GREETER_ITEM_SIZE_RELATIVE,
  GREETER_ITEM_SIZE_BOX,
};

enum _GreeterItemPosType {
  GREETER_ITEM_POS_UNSET,
  GREETER_ITEM_POS_ABSOLUTE,
  GREETER_ITEM_POS_RELATIVE,
};

enum _GreeterItemShowModes {
  GREETER_ITEM_SHOW_EVERYWHERE = 0xffff,
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
  GreeterItemPosType x_type;
  GreeterItemPosType y_type;
  double x;
  double y;

  GreeterItemShowModes show_modes;
  char *show_type; /* timed, system, config */
  char *show_subtype; /* halt, suspend, reboot */
  
  GreeterItemSizeType width_type;
  GreeterItemSizeType height_type;
  double width;
  double height;

  /* For packed items */
  gboolean expand;
  
  char *id;

  /* Button can propagate states and collect states from underlying items */
  gboolean button;

  GreeterItemType item_type;

  char *files[GREETER_ITEM_STATE_MAX];
  gdouble alphas[GREETER_ITEM_STATE_MAX];
  gboolean have_tint[GREETER_ITEM_STATE_MAX];
  guint32 tints[GREETER_ITEM_STATE_MAX];
  GdkPixbuf *orig_pixbufs[GREETER_ITEM_STATE_MAX];
  GdkPixbuf *pixbufs[GREETER_ITEM_STATE_MAX];

  gboolean have_color[GREETER_ITEM_STATE_MAX];
  guint32 colors[GREETER_ITEM_STATE_MAX];

  PangoFontDescription *fonts[GREETER_ITEM_STATE_MAX];
  char *orig_text;

  /* Container data */
  GList *fixed_children;
  
  GtkOrientation box_orientation;
  gboolean box_homogeneous;
  double box_x_padding;
  double box_y_padding;
  double box_min_width;
  double box_min_height;
  double box_spacing;
  GList *box_children;
  
  /* Runtime state: */
  GreeterItemState state;
  GreeterItemState base_state;
  gboolean mouse_down;
  gboolean mouse_over;

  /* Canvas data: */
  GnomeCanvasItem *item;
  GnomeCanvasGroup *group_item;

  /* geometry handling: */
  gboolean has_requisition;
  GtkRequisition requisition;
  GtkAllocation allocation;
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
GreeterItemInfo *greeter_item_find_my_button (GreeterItemInfo *info);

#endif /* GREETER_ITEM_H */
