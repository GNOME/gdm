#include "config.h"

#include <gtk/gtk.h>
#include <vicious.h>

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
  VeConfig *cfg;
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
			  ve_sure_string (GdmServAuthDir),
			  ve_sure_string (g_getenv ("DISPLAY")));
  cfg = ve_config_get (file);
  g_free (file);
  ve_config_set_string (cfg, item->id, ve_sure_string (id));
  ve_config_save (cfg, FALSE);
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

  populate_list (tm, selection, item->list_items);
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
