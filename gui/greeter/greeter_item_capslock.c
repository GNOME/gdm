#include "config.h"

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>

#include "greeter_parser.h"
#include "greeter_item_capslock.h"


static gboolean caps_lock_state = FALSE;

static Display *
get_parent_display (void)
{
  static gboolean tested = FALSE;
  static Display *dsp = NULL;

  if (tested)
    return dsp;

  tested = TRUE;

  if (g_getenv ("GDM_PARENT_DISPLAY") != NULL)
    {
      char *old_xauth = g_strdup (g_getenv ("XAUTHORITY"));
      if (g_getenv ("GDM_PARENT_XAUTHORITY") != NULL)
        {
	  gnome_setenv ("XAUTHORITY",
			g_getenv ("GDM_PARENT_XAUTHORITY"), TRUE);
	}
      dsp = XOpenDisplay (g_getenv ("GDM_PARENT_DISPLAY"));
      if (old_xauth != NULL)
        gnome_setenv ("XAUTHORITY", old_xauth, TRUE);
      else
        gnome_unsetenv ("XAUTHORITY");
      g_free (old_xauth);
    }

  return dsp;
}

static gboolean
is_caps_lock_on (void)
{
  unsigned int states;
  Display *dsp;

  /* HACK! incredible hack, if this is set we get
   * indicator state from the parent display, since we must be inside an
   * Xnest */
  dsp = get_parent_display ();
  if (dsp == NULL)
    dsp = GDK_DISPLAY ();

  if (XkbGetIndicatorState (dsp, XkbUseCoreKbd, &states) != Success)
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
  
  g_signal_connect (G_OBJECT (window), "key_press_event",
		    G_CALLBACK (cl_key_press_event), NULL);
  return TRUE;
}

