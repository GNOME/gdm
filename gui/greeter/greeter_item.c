#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "greeter_item.h"

#ifndef _
#define _(x) (x)
#endif

GreeterItemInfo *
greeter_item_info_new (GreeterItemType type)
{
  GreeterItemInfo *info;
  int i;

  info = g_new0 (GreeterItemInfo, 1);
  info->item_type = type;

  info->anchor = GTK_ANCHOR_NW;
  info->x_type = GREETER_ITEM_POS_UNSET;
  info->y_type = GREETER_ITEM_POS_UNSET;
  info->width_type = GREETER_ITEM_SIZE_UNSET;
  info->height_type = GREETER_ITEM_SIZE_UNSET;

  for (i=0; i< GREETER_ITEM_STATE_MAX; i++)
    {
      info->alphas[i] = 1.0;
    }

  info->box_orientation = GTK_ORIENTATION_VERTICAL;
  
  info->state = GREETER_ITEM_STATE_NORMAL;

  return info;
}

void
greeter_item_info_free (GreeterItemInfo *info)
{
  int i;

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    if (info->pixbufs[i])
      gdk_pixbuf_unref (info->pixbufs[i]);

  g_free (info->id);
  g_free (info->orig_text);
  g_free (info);
}

void
greeter_item_update_text (GreeterItemInfo *info)
{
  char *text;
  if (info && info->item &&
      GNOME_IS_CANVAS_TEXT (info->item))
    {
      text = greeter_item_expand_text (info->orig_text);

      g_object_set (G_OBJECT (info->item),
		    "text", text,
		    NULL);

      g_free (text);
    }

}


static void
get_clock (char *str, int str_len)
{
  struct tm *the_tm;
  time_t the_time;

  time (&the_time);
  the_tm = localtime (&the_time);

  if (strftime (str, str_len, "%a %b %d, %I:%M %p", the_tm) == 0) {
    /* according to docs, if the string does not fit, the
     * contents of str are undefined, thus just use
     * ??? */
    strcpy (str, "???");
  }
  str [str_len-1] = '\0'; /* just for sanity */
}


char *
greeter_item_expand_text (const char *text)
{
  GString *str;
  const char *p;
  const char *q;
  int r;
  char buf[256];

  str = g_string_sized_new (strlen (text));

  p = text;
  
  while (*p)
    {
      if (*p == '%')
	{
	  p++;
	  switch (*p)
	    {
	    case '%':
	      g_string_append (str, "%");
	      break;
	    case 'h':
	      r = gethostname (buf, sizeof(buf));
	      if (r)
		g_string_append (str, "localhost");
	      else
		g_string_append (str, buf);
	      break;
	    case 'd':
	      r = getdomainname (buf, sizeof(buf));
	      if (r)
		g_string_append (str, "localdomain");
	      else
		g_string_append (str, buf);
	      break;
	    case 'c':
	      get_clock (buf, sizeof(buf));
	      g_string_append (str, buf);
	      break;
	    case 0:
	      g_warning ("Unescape %% at end of text\n");
	      goto bail;
	      break;
	    default:
	      g_warning ("unknown escape code %%%c in text\n", *p);
	    }
	  p++;
	}
      else
	{
	  q = strchr(p, '%');
	  if (q == NULL)
	    {
	      g_string_append (str, p);
	      break;
	    }
	  else
	    {
	      g_string_append_len (str, p, q - p);
	      p = q;
	    }
	}
    }
  
 bail:

  return g_string_free (str, FALSE);
}
