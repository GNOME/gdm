#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libgnomecanvas/libgnomecanvas.h>
#include "greeter_parser.h"
#include "greeter_item_clock.h"
#include "greeter_item_pam.h"
#include "greeter_events.h"
#include "greeter_action_language.h"

#define DEBUG_GREETER 1

GtkWidget *window;
GtkWidget *canvas;

#ifdef DEBUG_GREETER
static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *key, gpointer data)
{
	if (key->keyval == GDK_Escape) {
		gtk_main_quit ();

		return TRUE;
	}

	return FALSE;
}
#endif

static void
greeter_setup_items (void)
{

  greeter_item_clock_setup ();
  greeter_item_pam_setup ();
  greeter_item_capslock_setup (window);
  greeter_item_register_action_callback ("language_button",
					 greeter_action_language,
					 window);

}

int
main (int argc, char *argv[])
{
  gint w, h;
  gboolean res;

  gtk_init (&argc, &argv);

  w = gdk_screen_width ();
  h = gdk_screen_height ();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

#ifdef DEBUG_GREETER
  gtk_signal_connect (GTK_OBJECT (window), "key_press_event",
		      GTK_SIGNAL_FUNC (key_press_event), NULL);
#endif

  canvas = gnome_canvas_new_aa ();
  gnome_canvas_set_scroll_region (GNOME_CANVAS (canvas),
				  0.0, 0.0,
				  (double) w,
				  (double) h);
  gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
  gtk_window_set_default_size (GTK_WINDOW (window), w, h);
  gtk_container_add (GTK_CONTAINER (window), canvas);

  res = greeter_parse (argv[1], GNOME_CANVAS (canvas), w, h);

  if (!res)
    g_warning ("Failed to parse file!");


  greeter_setup_items ();
  gtk_widget_show_all (window);


  gtk_main ();

  return 0;
}
