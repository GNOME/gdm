#include <gtk/gtk.h>
#include <time.h>
#include "greeter_item_clock.h"
#include "greeter_parser.h"

static gboolean
update_clock (GreeterItemInfo *info)
{
  struct tm *the_tm;
  time_t the_time;
  gint time_til_next_min;

  greeter_item_update_text (info);

  time (&the_time);
  the_tm = localtime (&the_time);
  /* account for leap seconds */
  time_til_next_min = 60 - the_tm->tm_sec;
  time_til_next_min = (time_til_next_min>=0?time_til_next_min:0);

  gtk_timeout_add (time_til_next_min*1000, (GtkFunction)update_clock, info);

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
