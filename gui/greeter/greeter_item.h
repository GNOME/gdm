#ifndef __GREETER_ITEMS_H__
#define __GREETER_ITEMS_H__

#include <libgnomecanvas/libgnomecanvas.h>

typedef struct _GreeterItemInfo GreeterItemInfo;
typedef enum _GreeterItemState GreeterItemState;

enum _GreeterItemState {
  GREETER_ITEM_STATE_NORMAL,
  GREETER_ITEM_STATE_PRELIGHT,
  GREETER_ITEM_STATE_ACTIVE,
  GREETER_ITEM_STATE_MAX,
};

struct _GreeterItemInfo {
  GnomeCanvasItem *item;
  GnomeCanvasGroup *group_item;
  
  GreeterItemState state;
  gboolean mouse_down;
  gboolean mouse_over;
  char *id;
  char *orig_text;
  
  GdkPixbuf *pixbufs[GREETER_ITEM_STATE_MAX];

  gboolean have_color[GREETER_ITEM_STATE_MAX];
  guint32 colors[GREETER_ITEM_STATE_MAX];
};

gint greeter_item_event_handler (GnomeCanvasItem *item,
				 GdkEvent        *event,
				 gpointer         data);

GreeterItemInfo *greeter_item_info_new (void);
void greeter_item_info_free (GreeterItemInfo *info);

char *greeter_item_expand_text (const char *text);

void greeter_item_update_text (GreeterItemInfo *info);

#endif /* __GREETER_ITEMS_H__ */
