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

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gdm.h"
#include "gdmconfig.h"

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
  greeter_item_timed_update ();
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
 * Timed Login: On GTK events, increase delay to at least 30
 * seconds. Or the GDM_KEY_TIMED_LOGIN_DELAY, whichever is higher
 */

static gboolean
gdm_timer_up_delay (GSignalInvocationHint *ihint,
		    guint	           n_param_values,
		    const GValue	  *param_values,
		    gpointer		   data)
{
  int timeddelay = gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);

  if (greeter_current_delay < 30)
    greeter_current_delay = 30;
  if (greeter_current_delay < timeddelay)
    greeter_current_delay = timeddelay;
  return TRUE;
}      

gboolean
greeter_item_timed_setup (void)
{

  /* if in timed mode, delay timeout on keyboard or menu activity */
  if ( ! ve_string_empty (gdm_config_get_string (GDM_KEY_TIMED_LOGIN)))
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
  int timeddelay = gdm_config_get_int (GDM_KEY_TIMED_LOGIN_DELAY);

  if (timed_handler_id == 0 &&
      ! ve_string_empty (gdm_config_get_string (GDM_KEY_TIMED_LOGIN)) &&
      timeddelay > 0)
    {
      greeter_current_delay = timeddelay;
      timed_handler_id = g_timeout_add (1000, gdm_timer, NULL);
    }
}

void
greeter_item_timed_stop (void)
{
  if (timed_handler_id != 0)
    {
      g_source_remove (timed_handler_id);
      timed_handler_id = 0;
    }
}

gboolean
greeter_item_timed_is_timed (void)
{
  return timed_handler_id != 0;
}
