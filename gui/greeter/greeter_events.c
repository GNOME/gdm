#include "config.h"

#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>

#include "greeter_item.h"
#include "greeter_parser.h"
#include "greeter_events.h"

struct CallbackInfo {
  ActionFunc func;
  gpointer user_data;
}; 

static GHashTable *callback_hash = NULL;

static void
state_run (GreeterItemInfo *info,
	   GreeterItemState old_state)
{
  if (info->state != old_state &&
      info->item != NULL)
    {
      if (info->pixbufs[info->state])
	gnome_canvas_item_set (info->item,
			       "pixbuf", info->pixbufs[info->state],
			       NULL);
      if (info->have_color[info->state])
	gnome_canvas_item_set (info->item,
			       "fill_color_rgba", info->colors[info->state],
			       NULL);
      if (info->fonts[info->state])
	gnome_canvas_item_set (info->item,
			       "font_desc", info->fonts[info->state],
			       NULL);
    }
}

static void propagate_state (GreeterItemInfo *info,
			     GreeterItemState old_state);

static void
propagate_state_foreach (gpointer data, gpointer user_data)
{
  GreeterItemInfo *info = data;
  GreeterItemState state = GPOINTER_TO_INT (user_data);
  GreeterItemState old_state;

  old_state = info->state;
  info->state = state;

  propagate_state (info, old_state);
}

static void
propagate_state (GreeterItemInfo *info,
		 GreeterItemState old_state)
{
  state_run (info, old_state);

  g_list_foreach (info->fixed_children, propagate_state_foreach,
		  GINT_TO_POINTER (info->state));
  g_list_foreach (info->box_children, propagate_state_foreach,
		  GINT_TO_POINTER (info->state));
}

static void propagate_reset_state (GreeterItemInfo *info,
				   GreeterItemState old_state);

static void
propagate_reset_state_foreach (gpointer data, gpointer user_data)
{
  GreeterItemInfo *info = data;
  GreeterItemState old_state;

  old_state = info->state;
  info->state = info->base_state;

  propagate_state (info, old_state);
}

static void
propagate_reset_state (GreeterItemInfo *info,
		       GreeterItemState old_state)
{
  state_run (info, old_state);

  g_list_foreach (info->fixed_children, propagate_reset_state_foreach, NULL);
  g_list_foreach (info->box_children, propagate_reset_state_foreach, NULL);
}

void
greeter_item_run_action_callback (const char *id)
{
  struct CallbackInfo *cb_info;
  GreeterItemInfo *info;

  g_return_if_fail (id != NULL);

  if (callback_hash == NULL)
    return;

  info = greeter_lookup_id (id);
  if (info == NULL)
    return;

  cb_info = g_hash_table_lookup (callback_hash, info->id);

  if (cb_info)
    (*cb_info->func) (info, cb_info->user_data);
}

gint
greeter_item_event_handler (GnomeCanvasItem *item,
			    GdkEvent        *event,
			    gpointer         data)
{
  GreeterItemState old_state;
  GreeterItemInfo *info;
  GreeterItemInfo *button;

  info = data;
  button = greeter_item_find_my_button (info);
  if (button != NULL && button != info)
    {
      /* FIXME: this is a hack, we have not really left the container,
       * but the container gets a stupid leave event and
       * we send it an enter event back, it does two event propagations
       * one of them is pointless. */
      /* if this is a button, fake an event on the button */
      return greeter_item_event_handler (button->item,
					 event,
					 button);
    }
  
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
      greeter_item_run_action_callback (info->id);
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

  info->base_state = info->state;

  if (info->state != old_state)
    {
      if (info->button)
        {
          if (info->state == GREETER_ITEM_STATE_NORMAL)
            propagate_reset_state (info, old_state);
          else
            propagate_state (info, old_state);
	}
      else
        state_run (info, old_state);
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

