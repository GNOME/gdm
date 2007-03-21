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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "gdm.h"
#include "gdmwm.h"
#include "gdmconfig.h"
#include "gdmcommon.h"
#include "misc.h"

#include "gdm-common.h"

#include "greeter_item.h"
#include "greeter_configuration.h"

extern gboolean GdmHaltFound;
extern gboolean GdmRebootFound;
extern gboolean GdmCustomCmdFound;
extern gboolean *GdmCustomCmdsFound;
extern gboolean GdmSuspendFound;
extern gboolean GdmConfiguratorFound;

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

  if (type != GREETER_ITEM_TYPE_LIST)
    {
      for (i=0; i< GREETER_ITEM_STATE_MAX; i++)
        {
	  /* these happen to coincide for all
	     items but list */
          info->data.rect.alphas[i] = 0xff;
        }
    }

  info->box_orientation = GTK_ORIENTATION_VERTICAL;
  
  info->state = GREETER_ITEM_STATE_NORMAL;
  info->base_state = GREETER_ITEM_STATE_NORMAL;

  info->show_modes = GREETER_ITEM_SHOW_EVERYWHERE;

  info->canvasbutton = FALSE;
  info->gtkbutton    = FALSE;

  if (GREETER_ITEM_TYPE_IS_TEXT (info))
    {
      info->data.text.max_width = 0xffff;
      info->data.text.max_screen_percent_width = 90;
      info->data.text.real_max_width = 0;
    }

  return info;
}

void
greeter_item_info_free (GreeterItemInfo *info)
{
  int i;
  GList *list;

  for (i = 0; i < GREETER_ITEM_STATE_MAX; i++)
    {
      if (GREETER_ITEM_TYPE_IS_PIXMAP (info))
        {
          if (info->data.pixmap.pixbufs[i] != NULL)
            g_object_unref (G_OBJECT (info->data.pixmap.pixbufs[i]));
          if (info->data.pixmap.files[i] != NULL)
            g_free (info->data.pixmap.files[i]);
	}
      else if (GREETER_ITEM_TYPE_IS_TEXT (info))
        {
          if (info->data.text.fonts[i] != NULL)
            pango_font_description_free (info->data.text.fonts[i]);
	}
    }

  list = info->fixed_children;
  info->fixed_children = NULL;
  g_list_foreach (list, (GFunc) greeter_item_info_free, NULL);
  g_list_free (list);

  list = info->box_children;
  info->box_children = NULL;
  g_list_foreach (list, (GFunc) greeter_item_info_free, NULL);
  g_list_free (list);

  if (GREETER_ITEM_TYPE_IS_TEXT (info))
    g_free (info->data.text.orig_text);

  /* FIXME: what about custom list items! */

  g_free (info->id);
  g_free (info->show_type);

  memset (info, 0, sizeof (GreeterItemInfo));
  g_free (info);
}

void
greeter_item_update_text (GreeterItemInfo *info)
{
  char *text;
  if (info && info->item &&
      GNOME_IS_CANVAS_TEXT (info->item) &&
      GREETER_ITEM_TYPE_IS_TEXT (info))
    {
      text = gdm_common_expand_text (info->data.text.orig_text);

      g_object_set (G_OBJECT (info->item),
		    "markup", text,
		    NULL);

      g_free (text);
    }

}

gboolean
greeter_item_is_visible (GreeterItemInfo *info)
{
  static gboolean checked = FALSE;
  static gboolean GDM_IS_LOCAL = FALSE;
  static gboolean GDM_FLEXI_SERVER = FALSE;
  gboolean sysmenu = FALSE;	
  gint i = 0;

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

  if ((gdm_wm_screen.width < info->minimum_required_screen_width) ||
      (gdm_wm_screen.height < info->minimum_required_screen_height))
    return FALSE;

  sysmenu = gdm_config_get_bool (GDM_KEY_SYSTEM_MENU);

  if (( ! gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) ||
        ! sysmenu ||
        ! GdmConfiguratorFound) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "config") == 0))
	  return FALSE;

  if (( ! gdm_config_get_bool (GDM_KEY_CHOOSER_BUTTON) || ! sysmenu) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "chooser") == 0))
	  return FALSE;

  if ( ! sysmenu && info->show_type != NULL &&
      strcmp (info->show_type, "system") == 0)
	  return FALSE;

  if (( ! sysmenu || ! GdmHaltFound) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "halt") == 0))
	  return FALSE;
  if (( ! sysmenu || ! GdmRebootFound) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "reboot") == 0))
	  return FALSE;
  if (( ! sysmenu || ! GdmSuspendFound) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "suspend") == 0))
	  return FALSE;

  for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {      
      gchar * key_string = g_strdup_printf (_("custom_cmd%d"), i);
      if (( ! sysmenu || ! GdmCustomCmdsFound[i]) &&
          (info->show_type != NULL &&
           strcmp (info->show_type, key_string) == 0)) {
	      g_free (key_string);
	      return FALSE;
      }
      g_free (key_string);
  }

  if (( ! gdm_config_get_bool (GDM_KEY_TIMED_LOGIN_ENABLE) ||
          ve_string_empty (gdm_config_get_string (GDM_KEY_TIMED_LOGIN)) ||
          NULL == g_getenv("GDM_TIMED_LOGIN_OK")) &&
      (info->show_type != NULL &&
       strcmp (info->show_type, "timed") == 0))
	  return FALSE;

  return TRUE;
}
