#include "config.h"

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>

#include "gdm.h"
#include "vicious.h"

#include "greeter_parser.h"
#include "greeter_configuration.h"
#include "greeter_item_timed.h"

extern gint greeter_current_delay;

static guint timed_handler_id = 0;

static void
greeter_item_timed_update (void)
{
  GreeterItemInfo *info;

  info = greeter_lookup_id ("timed-label");
  if (info != NULL)
    {
      greeter_item_update_text (info);
    }
}

/*
 * Timed Login: Timer
 */

static gboolean
gdm_timer (gpointer data)
{
  if ((greeter_current_delay % 5) == 0)
    {
      greeter_item_timed_update ();
    }
  greeter_current_delay --;
  if ( greeter_current_delay <= 0 )
    {
      /* timed interruption */
      printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
      fflush (stdout);
    }
  return TRUE;
}

/*
 * Timed Login: On GTK events, increase delay to
 * at least 30 seconds. Or the TimedLoginDelay,
 * whichever is higher
 */

static gboolean
gdm_timer_up_delay (GSignalInvocationHint *ihint,
		    guint	           n_param_values,
		    const GValue	  *param_values,
		    gpointer		   data)
{
  if (greeter_current_delay < 30)
    greeter_current_delay = 30;
  if (greeter_current_delay < GdmTimedLoginDelay)
    greeter_current_delay = GdmTimedLoginDelay;
  return TRUE;
}      

gboolean
greeter_item_timed_setup (void)
{
  /* if in timed mode, delay timeout on keyboard or menu
   * activity */
  if ( ! ve_string_empty (GdmTimedLogin))
    {
      guint sid;

      g_type_class_ref (GTK_TYPE_MENU_ITEM);

      sid = g_signal_lookup ("activate",
			     GTK_TYPE_MENU_ITEM);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  gdm_timer_up_delay,
				  NULL /* data */,
				  NULL /* destroy_notify */);

      sid = g_signal_lookup ("key_press_event",
			     GTK_TYPE_WIDGET);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  gdm_timer_up_delay,
				  NULL /* data */,
				  NULL /* destroy_notify */);
      sid = g_signal_lookup ("button_press_event",
			     GTK_TYPE_WIDGET);
      g_signal_add_emission_hook (sid,
				  0 /* detail */,
				  gdm_timer_up_delay,
				  NULL /* data */,
				  NULL /* destroy_notify */);
    }

  return TRUE;
}

void
greeter_item_timed_start (void)
{
  if (timed_handler_id == 0 &&
      ! ve_string_empty (GdmTimedLogin) &&
      GdmTimedLoginDelay > 0)
    {
      greeter_current_delay = GdmTimedLoginDelay;
      timed_handler_id = gtk_timeout_add (1000,
					  gdm_timer, NULL);
    }
}

void
greeter_item_timed_stop (void)
{
  if (timed_handler_id != 0)
    {
      gtk_timeout_remove (timed_handler_id);
      timed_handler_id = 0;
    }
}
