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
#include <time.h>
#include "greeter_item_clock.h"
#include "greeter_parser.h"

static gboolean
update_clock (gpointer data)
{
  GreeterItemInfo *info = data;
  struct tm *the_tm;
  time_t the_time;
  gint time_til_next_min;

  greeter_item_update_text (info);

  time (&the_time);
  the_tm = localtime (&the_time);
  /* account for leap seconds */
  time_til_next_min = 60 - the_tm->tm_sec;
  time_til_next_min = (time_til_next_min>=0?time_til_next_min:0);

  g_timeout_add (time_til_next_min*1000, update_clock, info);

  return FALSE;
}


gboolean
greeter_item_clock_setup (void)
{
  GreeterItemInfo *info;	

  info = greeter_lookup_id ("clock");
  if (info)
    update_clock (info);

  return TRUE;
}

void
greeter_item_clock_update (void)
{
  GreeterItemInfo *info;	

  info = greeter_lookup_id ("clock");
  if (info)
    greeter_item_update_text (info);
}
