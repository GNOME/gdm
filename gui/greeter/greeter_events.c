#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#include "greeter_item.h"
#include "greeter_events.h"

struct CallbackInfo {
  ActionFunc func;
  gpointer user_data;
}; 

static GHashTable *callback_hash = NULL;

gint
greeter_item_event_handler (GnomeCanvasItem *item,
			    GdkEvent        *event,
			    gpointer         data)
{
  GreeterItemState old_state;
  GreeterItemInfo *info;

  info = data;
  
  old_state = info->state;
  
  switch (event->type) {
  case GDK_ENTER_NOTIFY:
    info->mouse_over = TRUE;
    break;
    
  case GDK_LEAVE_NOTIFY:
    info->mouse_over = FALSE;
    break;
    
  case GDK_BUTTON_PRESS:
    info->mouse_down = TRUE;
    break;
    
  case GDK_BUTTON_RELEASE:
    info->mouse_down = FALSE;

    if (info->mouse_over && info->id && callback_hash)
      {
	struct CallbackInfo *cb_info;

	cb_info = g_hash_table_lookup (callback_hash, info->id);

	if (cb_info)
	  (*cb_info->func) (info, cb_info->user_data);
      }	    
    break;
    
  default:
    break;
  }

  if (info->mouse_over)
    {
      if (info->mouse_down)
	info->state = GREETER_ITEM_STATE_ACTIVE;
      else
	info->state = GREETER_ITEM_STATE_PRELIGHT;
    }
  else
    info->state = GREETER_ITEM_STATE_NORMAL;

  
  if (info->state != old_state)
    {
      if (info->pixbufs[info->state])
	gnome_canvas_item_set (item,
			       "pixbuf", info->pixbufs[info->state],
			       NULL);
      if (info->have_color[info->state])
	gnome_canvas_item_set (item,
			       "fill_color_rgba", info->colors[info->state],
			       NULL);
    }

  return FALSE;
}

void
greeter_item_register_action_callback (char            *id,
				       ActionFunc       func,
				       gpointer         user_data)
{
  struct CallbackInfo *info;

  if (callback_hash == NULL)
    callback_hash = g_hash_table_new_full (g_str_hash,
					   g_str_equal,
					   g_free,
					   g_free);

  info = g_new (struct CallbackInfo, 1);

  info->func = func;
  info->user_data = user_data;

  g_hash_table_insert (callback_hash,
		       g_strdup (id),
		       info);
}

