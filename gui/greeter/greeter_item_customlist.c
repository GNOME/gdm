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

#include "gdm.h"
#include "gdmconfig.h"

#include "greeter_item.h"
#include "greeter_configuration.h"
#include "greeter_item_customlist.h"
#include "greeter_parser.h"

enum
{
  GREETER_LIST_TEXT = 0,
  GREETER_LIST_ID
};

static void
row_selected (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};
  GreeterItemInfo *item = data;
  char *id = NULL;
  char *file;

  if (DOING_GDM_DEVELOPMENT)
    return;

  if (ve_string_empty (item->id))
    return;

  if (gtk_tree_selection_get_selected (selection, &tm, &iter))
    {
      gtk_tree_model_get (tm, &iter, GREETER_LIST_ID,
			  &id, -1);
    }

  file = g_strdup_printf ("%s/%s.GreeterInfo",
			  ve_sure_string (gdm_config_get_string (GDM_KEY_SERV_AUTHDIR)),
			  ve_sure_string (g_getenv ("DISPLAY")));

  gdm_set_servauth (file, item->id, id);
}

static void
populate_list (GtkTreeModel *tm, GtkTreeSelection *selection, GList *list_items)
{
  GList *li;

  for (li = list_items; li != NULL; li = li->next)
    {
      GreeterItemListItem *litem = li->data;
      GtkTreeIter iter = {0};
      gtk_list_store_append (GTK_LIST_STORE (tm), &iter);
      gtk_list_store_set (GTK_LIST_STORE (tm), &iter,
			  GREETER_LIST_TEXT, litem->text,
			  GREETER_LIST_ID, litem->id,
			  -1);
      /* select first item */
      if (li == list_items)
        gtk_tree_selection_select_iter (selection, &iter);
    }
}

static void
setup_customlist (GtkWidget *tv, GreeterItemInfo *item)
{
  GtkTreeModel *tm;
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection;

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv),
				     FALSE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

  g_signal_connect (selection, "changed",
		    G_CALLBACK (row_selected),
		    item);

  tm = (GtkTreeModel *)gtk_list_store_new (2,
					   G_TYPE_STRING,
					   G_TYPE_STRING);
  gtk_tree_view_set_model (GTK_TREE_VIEW (tv), tm);
      
  column = gtk_tree_view_column_new_with_attributes
	  ("Choice",
	   gtk_cell_renderer_text_new (),
	   "text", GREETER_LIST_TEXT,
	   NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);

  populate_list (tm, selection, item->data.list.items);
}

gboolean
greeter_item_customlist_setup (void)
{
  const GList *custom_items = greeter_custom_items ();
  const GList *li;
  for (li = custom_items; li != NULL; li = li->next)
    {
      GreeterItemInfo *info = li->data;

      if (info != NULL &&
	  info->item_type == GREETER_ITEM_TYPE_LIST &&
	  info->item != NULL &&
          GNOME_IS_CANVAS_WIDGET (info->item))
        {
          GtkWidget *sw = GNOME_CANVAS_WIDGET (info->item)->widget;
          if (GTK_IS_SCROLLED_WINDOW (sw) && 
	      GTK_IS_TREE_VIEW (GTK_BIN (sw)->child))
            {
	      setup_customlist (GTK_BIN (sw)->child, info);
            }
        }
    }
  return TRUE;
}
