#include "config.h"

#include <libgnome/libgnome.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/utsname.h>

#include "greeter_item.h"
#include "greeter_configuration.h"
#include "vicious.h"

extern gint greeter_current_delay;

GreeterItemInfo *
greeter_item_info_new (GreeterItemInfo *parent,
		       GreeterItemType  type)
{
  GreeterItemInfo *info;
  int i;

  info = g_new0 (GreeterItemInfo, 1);
  info->item_type = type;
  info->parent = parent;

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
  info->base_state = GREETER_ITEM_STATE_NORMAL;

  info->show_modes = GREETER_ITEM_SHOW_EVERYWHERE;

  info->button = FALSE;

  return info;
}

void
greeter_item_info_free (GreeterItemInfo *info)
{
  int i;
  GList *list;

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (info->pixbufs[i])
        g_object_unref (G_OBJECT (info->pixbufs[i]));
      if (info->orig_pixbufs[i])
        g_object_unref (G_OBJECT (info->orig_pixbufs[i]));
      if (info->files[i])
        g_free (info->files[i]);
      if (info->fonts[i])
        pango_font_description_free (info->fonts[i]);
    }

  list = info->fixed_children;
  info->fixed_children = NULL;
  g_list_foreach (list, (GFunc) greeter_item_info_free, NULL);
  g_list_free (list);

  list = info->box_children;
  info->box_children = NULL;
  g_list_foreach (list, (GFunc) greeter_item_info_free, NULL);
  g_list_free (list);

  g_free (info->id);
  g_free (info->orig_text);
  g_free (info->show_type);

  memset (info, 0, sizeof (GreeterItemInfo));
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
		    "markup", text,
		    NULL);

      g_free (text);
    }

}


static char *
get_clock (void)
{
  struct tm *the_tm;
  time_t the_time;

  time (&the_time);
  the_tm = localtime (&the_time);

  if (GdmUse24Clock) 
    {
      return ve_strftime (the_tm, _("%a %b %d, %H:%M"));
    } 
  else 
    {
      return ve_strftime (the_tm, _("%a %b %d, %I:%M %p"));
    }
}


char *
greeter_item_expand_text (const char *text)
{
  GString *str;
  const char *p;
  char *clock;
  int r;
  int underline = -1;
  char buf[256];
  struct utsname name;

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
	    case 'n':
	      uname (&name);
	      g_string_append (str, name.nodename);
	      break;
	    case 'h':
	      buf[sizeof(buf) - 1] = '\0';
	      r = gethostname (buf, sizeof(buf) - 1);
	      if (r)
		g_string_append (str, "localhost");
	      else
		g_string_append (str, buf);
	      break;
	    case 'o':
	      buf[sizeof(buf) - 1] = '\0';
	      r = getdomainname (buf, sizeof(buf) - 1);
	      if (r)
		g_string_append (str, "localdomain");
	      else
		g_string_append (str, buf);
	      break;
	    case 'd':
	      g_string_append_printf (str, "%d",
				      greeter_current_delay);
	      break;
	    case 's':
	      g_string_append (str, ve_sure_string (GdmTimedLogin));
	      break;
	    case 'c':
	      clock = get_clock ();
	      g_string_append (str, clock);
	      g_free (clock);
	      break;
	    case 0:
	      g_warning ("Unescaped %% at end of text\n");
	      goto bail;
	      break;
	    default:
	      g_warning ("unknown escape code %%%c in text\n", *p);
	    }
	}
      else if (*p == '_')
        {
	  underline = g_utf8_skip[*(guchar *)(p+1)];
	  g_string_append (str, "<u>");
	}
      else
	{
	  g_string_append_c (str, *p);
	  if (underline >= 0)
	    {
	      underline--;
	      if (underline == 0)
	        g_string_append (str, "</u>");
	    }
	}
      p++;
    }
  
 bail:

  if (underline >= 0)
    g_string_append (str, "</u>");

  return g_string_free (str, FALSE);
}

gboolean
greeter_item_is_visible (GreeterItemInfo *info)
{
  static gboolean checked = FALSE;
  static gboolean GDM_IS_LOCAL = FALSE;
  static gboolean GDM_FLEXI_SERVER = FALSE;

  if ( ! checked)
    {
      if (g_getenv ("GDM_IS_LOCAL") != NULL)
	GDM_IS_LOCAL = TRUE;
      if (g_getenv ("GDM_FLEXI_SERVER") != NULL)
	GDM_FLEXI_SERVER = TRUE;
    }

  if (GDM_IS_LOCAL && ! GDM_FLEXI_SERVER &&
      ! (info->show_modes & GREETER_ITEM_SHOW_CONSOLE_FIXED))
    return FALSE;
  if (GDM_IS_LOCAL && GDM_FLEXI_SERVER &&
      ! (info->show_modes & GREETER_ITEM_SHOW_CONSOLE_FLEXI))
    return FALSE;
  if ( ! GDM_IS_LOCAL && GDM_FLEXI_SERVER &&
      ! (info->show_modes & GREETER_ITEM_SHOW_REMOTE_FLEXI))
    return FALSE;
  if ( ! GDM_IS_LOCAL && ! GDM_FLEXI_SERVER &&
      ! (info->show_modes & GREETER_ITEM_SHOW_REMOTE))
    return FALSE;

  if (( ! GdmConfigAvailable || ! GdmSystemMenu) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "config") == 0)
	  return FALSE;

  if (( ! GdmChooserButton || ! GdmSystemMenu) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "chooser") == 0)
	  return FALSE;

  if ( ! GdmSystemMenu &&
      info->show_type != NULL &&
      strcmp (info->show_type, "system") == 0)
	  return FALSE;

  if (( ! GdmSystemMenu || GdmHalt == NULL) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "halt") == 0)
	  return FALSE;
  if (( ! GdmSystemMenu || GdmReboot == NULL) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "reboot") == 0)
	  return FALSE;
  if (( ! GdmSystemMenu || GdmSuspend == NULL) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "suspend") == 0)
	  return FALSE;

  if (ve_string_empty (GdmTimedLogin) &&
      info->show_type != NULL &&
      strcmp (info->show_type, "timed") == 0)
	  return FALSE;

  return TRUE;
}

GreeterItemInfo *
greeter_item_find_my_button (GreeterItemInfo *info)
{
  if (info == NULL)
    return NULL;
  else if (info->button)
    return info;
  else
    return greeter_item_find_my_button (info->parent);
}
