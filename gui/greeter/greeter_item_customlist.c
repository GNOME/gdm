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

#include "gdm.h"
#include "gdmconfig.h"
#include "gdmsession.h"

#include "greeter_item.h"
#include "greeter_configuration.h"
#include "greeter_item_customlist.h"
#include "greeter_parser.h"
#include "greeter_session.h"
#include "greeter_action_language.h"

/*
 * Keep track of the session/language widgets so we can
 * set their values when the session/language dialogs are
 * changed.
 */
static GtkWidget  *session_widget  = NULL;
static GtkWidget  *language_widget = NULL;
static gchar      *session_key     = NULL;

extern GList      *sessions;
extern GHashTable *sessnames;

enum
{
  GREETER_LIST_TEXT = 0,
  GREETER_LIST_ID
};

/*
 * This function sets the custom list when the session list has changed in
 * the session dialog (launched from session button or F10 menu).
 */
void
greeter_custom_set_session (gchar *session)
{
  GList *tmp;
  int i=0;

  /*
   * Since the sessions are created before the session list is generated,
   * keep track of the session and set active row when it is setup.  This
   * function will get a NULL when the session is initialized to NULL
   * at startup, so just return.
   */
  if (session == NULL)
    return;
  else
    {
      /*
       * If the session_widget hasn't been setup yet (which it won't be when
       * the greeter_sessioninit function is called, then just store the 
       * session and we'll set the value when the combo box is initialized later.
       */
      g_free (session_key);
      session_key = g_strdup (session);
    }

  /* Do nothing if there is no session widget */
  if (session_widget == NULL)
     return;

  /* Last isn't in the session list, so handle separate. */
  if (strcmp (session, LAST_SESSION) == 0)
    {
      if (GTK_IS_COMBO_BOX (session_widget))
        {
          gtk_combo_box_set_active (GTK_COMBO_BOX (session_widget), 0);
        }
      else if (GTK_IS_SCROLLED_WINDOW (session_widget) && 
               GTK_IS_TREE_VIEW (GTK_BIN (session_widget)->child))
        {
          GtkTreeView  *tv = GTK_TREE_VIEW (GTK_BIN (session_widget)->child);
          GtkTreeModel *tm = gtk_tree_view_get_model (tv);
          GtkTreeSelection *selection = gtk_tree_view_get_selection (tv);
          GtkTreeIter loopiter;

          if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tm), &loopiter))
            gtk_tree_selection_select_iter (selection, &loopiter);
        }
    }

  /*
   * Handle for either combo box or list style, depending on which is being
   * used.
 . */
  if (GTK_IS_COMBO_BOX (session_widget))
    {
      for (tmp = sessions; tmp != NULL; tmp = tmp->next)
        {
          char *file;

          i++;
          file = tmp->data;
          if (strcmp (session, file) == 0)
            {
              gtk_combo_box_set_active (GTK_COMBO_BOX (session_widget), i);
              break;
            }
        }
    }
  else if (GTK_IS_SCROLLED_WINDOW (session_widget) && 
           GTK_IS_TREE_VIEW (GTK_BIN (session_widget)->child))
    {
      GtkTreeView  *tv = GTK_TREE_VIEW (GTK_BIN (session_widget)->child);
      GtkTreeModel *tm = gtk_tree_view_get_model (tv);
      GtkTreeSelection *selection = gtk_tree_view_get_selection (tv);
      GtkTreeIter loopiter;

      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tm), &loopiter))
        {
          do
            {
              char *file;

              gtk_tree_model_get (GTK_TREE_MODEL (tm), &loopiter, GREETER_LIST_ID, &file, -1);
              if (file != NULL && strcmp (session, file) == 0)
                {
                   GtkTreePath *path = gtk_tree_model_get_path (tm, &loopiter);

                   gtk_tree_selection_select_iter (selection, &loopiter);
                   gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv),
                                                 path, NULL,
                                                 FALSE, 0.0, 0.0);
                  gtk_tree_path_free (path);
                  break;
               }
           } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (tm), &loopiter));
        }
    }
}

/*
 * This function sets the custom list when the language list has changed in
 * the language dialog (launched from language button or F10 menu).
 */
void
greeter_custom_set_language (gchar *language)
{
  GtkListStore *lang_model = greeter_language_get_model ();
  GtkTreeIter iter;
  gboolean valid;
  char *locale_name;
  int i=0;

  /* Do nothing if there is no language widget */
  if (language_widget == NULL)
     return;

  /*
   * Handle for either combo box or list style, depending on which is being
   * used.
 . */
  if (GTK_IS_COMBO_BOX (language_widget))
    {
      valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (lang_model),
         &iter);
      while (valid)
        {
          gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter,
             LOCALE_COLUMN, &locale_name, -1);

          if (strcmp (language, locale_name) == 0)
            {
              gtk_combo_box_set_active (GTK_COMBO_BOX (language_widget), i);
              break;
            }
          valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (lang_model), &iter);
          i++;
       }
    }
  else if (GTK_IS_SCROLLED_WINDOW (language_widget) && 
           GTK_IS_TREE_VIEW (GTK_BIN (language_widget)->child))
    {
      GtkTreeView  *tv = GTK_TREE_VIEW (GTK_BIN (language_widget)->child);
      GtkTreeModel *tm = gtk_tree_view_get_model (tv);
      GtkTreeSelection *selection = gtk_tree_view_get_selection (tv);
      GtkTreeIter loopiter;

      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tm), &loopiter))
        {
          do
            {
              char *locale_file;

              gtk_tree_model_get (GTK_TREE_MODEL (tm), &loopiter, GREETER_LIST_ID, &locale_file, -1);
              if (locale_file != NULL && strcmp (language, locale_file) == 0)
                {
                   GtkTreePath *path = gtk_tree_model_get_path (tm, &loopiter);

                   gtk_tree_selection_select_iter (selection, &loopiter);
                   gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv),
                                                 path, NULL,
                                                 FALSE, 0.0, 0.0);
                  gtk_tree_path_free (path);
                  break;
               }
           } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (tm), &loopiter));
        }
    }
}

/* Helper function to set custom list values */
static void
populate_list (GtkTreeModel *tm, GtkTreeSelection *selection, GList *list_items)
{
  GList *li;

  for (li = list_items; li != NULL; li = li->next)
    {
      GtkTreeIter iter = {0};
      GreeterItemListItem *litem = li->data;
      gtk_list_store_append (GTK_LIST_STORE (tm), &iter);
      gtk_list_store_set (GTK_LIST_STORE (tm), &iter,
			  GREETER_LIST_TEXT, litem->text,
			  GREETER_LIST_ID,   litem->id,
			  -1);
    }
}

/* Helper function to set session values */
static void
populate_session (GObject * object)
{
  GList *tmp;

  /* Last isn't in the session list, so add separate. */
  if (GTK_IS_COMBO_BOX (object))
    gtk_combo_box_append_text (GTK_COMBO_BOX (object), _("Last session"));
  else if (GTK_IS_TREE_MODEL (object))
    {
      GtkTreeIter loopiter;
      GtkTreeModel *tm = GTK_TREE_MODEL (object);

      gtk_list_store_append (GTK_LIST_STORE (tm), &loopiter);

      gtk_list_store_set (GTK_LIST_STORE (tm), &loopiter,
         GREETER_LIST_TEXT, _("Last session"),
         GREETER_LIST_ID,   LAST_SESSION,
         -1);
     }

  /* Loop through the sessions and set the custom list values. */
  for (tmp = sessions; tmp != NULL; tmp = tmp->next)
    {
      GdmSession *session;
      char *file;

      file = (char *) tmp->data;
      session = g_hash_table_lookup (sessnames, file);

      if (GTK_IS_COMBO_BOX (object))
        {
          if (session->clearname != NULL)
             gtk_combo_box_append_text (GTK_COMBO_BOX (object), (session->clearname));
          else
             gtk_combo_box_append_text (GTK_COMBO_BOX (object), (session->name));
        }
      else if (GTK_IS_TREE_MODEL (object))
        {
          GtkTreeIter loopiter;
          GtkTreeModel *tm = GTK_TREE_MODEL (object);
          gchar *to_display;

          gtk_list_store_append (GTK_LIST_STORE (tm), &loopiter);
          if (session->clearname != NULL)
             to_display = session->clearname;
          else
             to_display = session->name;

          gtk_list_store_set (GTK_LIST_STORE (tm), &loopiter,
             GREETER_LIST_TEXT, to_display,
             GREETER_LIST_ID,   file,
             -1);
        }
    }

  /*
   * Set the session if one exists, this will calback and set the
   * custom list
   */
  if (session_key != NULL)
    greeter_custom_set_session (session_key);
}

/* Helper function to set language values */
static void
populate_language (GObject *object)
{
  GtkListStore *lang_model = greeter_language_get_model ();
  GtkTreeIter iter;
  char *name, *untranslated, *display_name, *locale_name;
  gboolean valid;

  valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (lang_model),
     &iter);
  while (valid)
    {
      gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter,
         TRANSLATED_NAME_COLUMN, &name,
         UNTRANSLATED_NAME_COLUMN, &untranslated,
         LOCALE_COLUMN, &locale_name, -1);

      if (untranslated)
         display_name = g_strdup_printf ("%s (%s)", name, untranslated);
      else
         display_name = g_strdup (name);

      if (GTK_IS_COMBO_BOX (object))
         gtk_combo_box_append_text (GTK_COMBO_BOX (object), display_name);
      else if (GTK_IS_TREE_MODEL (object))
        {
          GtkTreeIter loopiter;
          GtkTreeModel *tm = GTK_TREE_MODEL (object);

          gtk_list_store_append (GTK_LIST_STORE (tm), &loopiter);
          gtk_list_store_set (GTK_LIST_STORE (tm), &loopiter,
             GREETER_LIST_TEXT, display_name,
             GREETER_LIST_ID,   locale_name,
             -1);
        }
    valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (lang_model), &iter);
  }
}

/* Callback helper function to set session value */
static void
combo_session_selected (char *session_val)
{
  GList *tmp;
  char *file;

  /* Last isn't in the session list, so add separate. */
  if (strcmp (session_val, _("Last session")) == 0)
     greeter_set_session (LAST_SESSION);
  else 
    {
      /*
       * Loop through the sessions to find the row the
       * user has selected, and set that session.
       */
      for (tmp = sessions; tmp != NULL; tmp = tmp->next)
        {
          GdmSession *session;
          char *name;

          file    = tmp->data;
          session = g_hash_table_lookup (sessnames, file);

          if (session->clearname)
             name = session->clearname;
          else
             name = session->name;

          if (strcmp (name, session_val) == 0)
            {
              greeter_set_session (file);
              break;
            }
        }
    }
}

/* Callback function for combo style custom lists */ 
static void
combo_selected (GtkComboBox *combo, GreeterItemInfo *item)
{
  char  *id = NULL;
  char  *file;
  char  *active;

  if (ve_string_empty (item->id))
    return;
 
  active = gtk_combo_box_get_active_text (combo);

  if (strcmp (item->id, "session") == 0)
    {
      combo_session_selected (active);
    }
  else if (strcmp (item->id, "language") == 0)
    {
      /*
       * Since combo boxes can't store the ID value, have to do some
       * extra work to figure out which row is selected.
       */
      GtkListStore *lang_model = greeter_language_get_model ();
      GtkTreeIter iter;
      char *name, *untranslated, *display_name, *locale_name;
      gboolean valid;

      valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (lang_model),
	 &iter);
      while (valid)
        {
          gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter,
             TRANSLATED_NAME_COLUMN, &name,
             UNTRANSLATED_NAME_COLUMN, &untranslated,
             LOCALE_COLUMN, &locale_name, -1);

          if (untranslated)
             display_name = g_strdup_printf ("%s (%s)", name, untranslated);
          else
             display_name = g_strdup (name);

          if (strcmp (display_name, active) == 0)
            {
              greeter_language_set (locale_name);
              break;
            }
          g_free (display_name);
          valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (lang_model), &iter);
        }
    }
  else
    {
      if (DOING_GDM_DEVELOPMENT)
        return;

      id   = gtk_combo_box_get_active_text (combo);
      file = g_strdup_printf ("%s/%s.GreeterInfo",
             ve_sure_string (gdm_config_get_string (GDM_KEY_SERV_AUTHDIR)),
             ve_sure_string (g_getenv ("DISPLAY")));

      gdm_save_customlist_data (file, item->id, id);
   }
}

/* Setup combo sytle custom list */
static void
setup_combo_customlist (GtkComboBox *combo, GreeterItemInfo *item)
{
  GList *li;

  g_signal_connect (G_OBJECT (combo), "changed",
                    G_CALLBACK (combo_selected), item);

  /* Make sure that focus never leaves username/password entry */
  gtk_combo_box_set_focus_on_click (combo, FALSE);

  if (strcmp (item->id, "session") == 0)
    {
      populate_session (G_OBJECT (combo));
      /*
       * Do not select since the session_init will initialize the
       * value and cause the combo list to get set without needing
       * to do it here.
       */
    }
  else if (strcmp (item->id, "language") == 0)
    {
      populate_language (G_OBJECT (combo));
      /* Select first */
      gtk_combo_box_set_active (combo, 0);
    }
  else
    {
      for (li = item->data.list.items; li != NULL; li = li->next)
       {
          GreeterItemListItem *litem = li->data;
          gtk_combo_box_append_text (combo, litem->text);
       }
      /* Select first */
      gtk_combo_box_set_active (combo, 0);
    }
}

/* Callback function for list style custom lists */
static void
list_selected (GtkTreeSelection *selection, GreeterItemInfo *item)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};
  char *id = NULL;
  char *file;

  if (ve_string_empty (item->id))
    return;

  if (gtk_tree_selection_get_selected (selection, &tm, &iter))
    {
      gtk_tree_model_get (tm, &iter, GREETER_LIST_ID,
                          &id, -1);
    }

  /* 
   * Note for session and language we are using the id to store the
   * value to pass in.
   */
  if (strcmp (item->id, "session") == 0)
    {
      if (id != NULL)
        greeter_set_session (id);
    }
  else if (strcmp (item->id, "language") == 0)
    {
      if (id != NULL)
        greeter_language_set (id);
    }
  else
    {
      if (DOING_GDM_DEVELOPMENT)
        return;

      file = g_strdup_printf ("%s/%s.GreeterInfo",
        ve_sure_string (gdm_config_get_string (GDM_KEY_SERV_AUTHDIR)),
        ve_sure_string (g_getenv ("DISPLAY")));

      gdm_save_customlist_data (file, item->id, id);
   }
}

static gboolean custom_list_release_event (GtkWidget *widget,
                                           GdkEventSelection *event,
                                           gpointer user_data)
{
  GreeterItemInfo *entry_info = greeter_lookup_id ("user-pw-entry");

  /* Make sure that focus never leaves username/password entry */
  if (entry_info && entry_info->item &&
      GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
      GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
    {
      GtkWidget *entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
      gtk_widget_grab_focus (entry);
    }
    return FALSE;
}

/* Setup custom list style */
static void
setup_customlist (GtkWidget *tv, GreeterItemInfo *item)
{
  GtkTreeModel *tm;
  GtkTreeViewColumn *column;
  GtkTreeSelection *selection;
  GtkTreeIter iter = {0};

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv),
				     FALSE);
  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_BROWSE);

  g_signal_connect (selection, "changed",
		    G_CALLBACK (list_selected),
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

  g_signal_connect (tv, "button_release_event",
                    G_CALLBACK (custom_list_release_event),
                    NULL);

  if (strcmp (item->id, "session") == 0)
    {
      populate_session (G_OBJECT (tm));
      /*
       * Do not select since the session_init will initialize the
       * value and cause the combo list to get set without needing
       * to do it here.
       */
    }
  else if (strcmp (item->id, "language") == 0)
    {
      populate_language (G_OBJECT (tm));

      /* Select first item */
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tm), &iter))
        gtk_tree_selection_select_iter (selection, &iter);
    }
  else
    {
      populate_list (tm, selection, item->data.list.items);

      /* Select first item */
      if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (tm), &iter))
        gtk_tree_selection_select_iter (selection, &iter);
    }
}

/*
 * This function initializes all custom lists aside from
 * the userlist (face browser), which is handled 
 * separately.
 */
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

          /*
           * Store these so that when the values change in the 
           * F10 session/language dialogs, we can easily modify
           * them.
           */
          if (strcmp (info->id, "session") == 0)
             session_widget = sw;
          else if (strcmp (info->id, "language") == 0)
             language_widget = sw;

          /* If combo or list style, process appropriately */
          if (GTK_IS_SCROLLED_WINDOW (sw) && 
	      GTK_IS_TREE_VIEW (GTK_BIN (sw)->child))
            {
	      setup_customlist (GTK_BIN (sw)->child, info);
            }
          else if (GTK_IS_COMBO_BOX (sw))
            {
              setup_combo_customlist (GTK_COMBO_BOX (sw), info);
            }
        }
    }
  return TRUE;
}

