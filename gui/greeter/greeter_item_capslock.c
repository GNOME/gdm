#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include "greeter_parser.h"
#include "greeter_item_capslock.h"


static gboolean caps_lock_state = FALSE;

static gboolean
is_caps_lock_on (void)
{
  unsigned int states;
	
  if (XkbGetIndicatorState (GDK_DISPLAY (), XkbUseCoreKbd, &states) != Success)
      return FALSE;

  return (states & ShiftMask) != 0;
}


static void
capslock_update (gboolean new_state)
{
  GreeterItemInfo *info;
  GnomeCanvasItem *item;

  caps_lock_state = new_state;
  
  info = greeter_lookup_id ("caps-lock-warning");

  if (info)
    {
      if (info->group_item != NULL)
	item = GNOME_CANVAS_ITEM (info->group_item);
      else
	item = info->item;

      if (caps_lock_state)
        {
	  gnome_canvas_item_show (item);
	}
      else
        {
	  gnome_canvas_item_hide (item);
	}
    }
}

static gboolean
cl_key_press_event (GtkWidget *widget, GdkEventKey *key, gpointer data)
{
  gboolean new_state;

  new_state = is_caps_lock_on ();
  if (new_state != caps_lock_state)
    capslock_update (new_state);
  
  return FALSE;
}


gboolean
greeter_item_capslock_setup (GtkWidget *window)
{
  capslock_update (is_caps_lock_on ());
  
  gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
		      GTK_SIGNAL_FUNC (cl_key_press_event), NULL);
  return TRUE;
}

