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

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "gdmlanguages.h"

#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_item_pam.h"
#include "greeter_action_language.h"
#include "greeter_parser.h"
#include "greeter_item_customlist.h"

#define LAST_LANGUAGE "Last"
#define DEFAULT_LANGUAGE "Default"

static GtkWidget    *tv                       = NULL;
static GtkListStore *lang_model               = NULL;
static GtkWidget    *dialog                   = NULL;
static gchar        *current_language         = NULL;
static gchar        *dialog_selected_language = NULL;
static gint          savelang                 = GTK_RESPONSE_NO;

GtkListStore *
greeter_language_get_model (void)
{
   return lang_model;
}

void
greeter_language_initialize_model (void)
{
  GList *list, *li;
  GtkTreeIter iter;

  list = gdm_lang_read_locale_file (gdm_config_get_string (GDM_KEY_LOCALE_FILE));

  lang_model = gtk_list_store_new (NUM_COLUMNS,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING);

  gtk_list_store_append (lang_model, &iter);
  gtk_list_store_set (lang_model, &iter,
		      TRANSLATED_NAME_COLUMN, _("Last language"),
		      UNTRANSLATED_NAME_COLUMN, NULL,
		      LOCALE_COLUMN, LAST_LANGUAGE,
		      -1);

  gtk_list_store_append (lang_model, &iter);
  gtk_list_store_set (lang_model, &iter,
		      TRANSLATED_NAME_COLUMN, _("System Default"),
		      UNTRANSLATED_NAME_COLUMN, NULL,
		      LOCALE_COLUMN, DEFAULT_LANGUAGE,
		      -1);

  for (li = list; li != NULL; li = li->next)
    {
      char *lang = li->data;
      char *name;
      char *untranslated;

      li->data = NULL;

      name = gdm_lang_name (lang,
			    FALSE /* never_encoding */,
			    TRUE /* no_group */,
			    FALSE /* untranslated */,
			    FALSE /* markup */);

      untranslated = gdm_lang_untranslated_name (lang,
						 TRUE /* markup */);

      gtk_list_store_append (lang_model, &iter);
      gtk_list_store_set (lang_model, &iter,
			  TRANSLATED_NAME_COLUMN, name,
			  UNTRANSLATED_NAME_COLUMN, untranslated,
			  LOCALE_COLUMN, lang,
			  -1);

      g_free (name);
      g_free (untranslated);
      g_free (lang);
    }
  g_list_free (list);
}

gint
greeter_language_get_save_language (void)
{
  return savelang;
}

gchar *
greeter_language_get_language (const char *old_language)
{
  gchar *retval = NULL;

  /* Don't save language unless told otherwise */
  savelang = GTK_RESPONSE_NO;

  if (old_language == NULL)
    old_language = "";

  /* If a different language is selected */
  if (current_language != NULL && strcmp (current_language, LAST_LANGUAGE) != 0)
    {
      if (strcmp (current_language, DEFAULT_LANGUAGE) == 0)
	retval = g_strdup ("");
      else
        retval = g_strdup (current_language);

      /* User's saved language is not the chosen one */
      if (strcmp (old_language, retval) != 0)
	{
	  gchar *primary_message;
	  gchar *secondary_message;
	  char *current_name, *saved_name;

	  if (strcmp (current_language, DEFAULT_LANGUAGE) == 0)
	    current_name = g_strdup (_("System Default"));
	  else
	    current_name = gdm_lang_name (current_language,
					  FALSE /* never_encoding */,
					  TRUE /* no_group */,
					  TRUE /* untranslated */,
					  TRUE /* markup */);
	  if (strcmp (old_language, "") == 0)
	    saved_name = g_strdup (_("System Default"));
	  else
	    saved_name = gdm_lang_name (old_language,
					FALSE /* never_encoding */,
					TRUE /* no_group */,
					TRUE /* untranslated */,
					TRUE /* markup */);

	  primary_message = g_strdup_printf (_("Do you wish to make %s the default for future sessions?"),
	                                     current_name);
 	  secondary_message = g_strdup_printf (_("You have chosen %s for this session, but your default setting is "
	                                         "%s."), current_name, saved_name);
	  g_free (current_name);
	  g_free (saved_name);

	  savelang = gdm_wm_query_dialog (primary_message, secondary_message,
		_("Make _Default"), _("Just For _This Session"), TRUE);
	  g_free (primary_message);
	  g_free (secondary_message);
	}
    }
  else
    {
      retval = g_strdup (old_language);
    }

  return retval;
}

static void
selection_changed (GtkTreeSelection *selection,
		   gpointer          data)
{
  GtkTreeIter iter;

  if (gtk_tree_selection_get_selected (selection, NULL, &iter))
    {
      g_free (dialog_selected_language);
      gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter, LOCALE_COLUMN, &dialog_selected_language, -1);
    }
}

static void
tree_row_activated (GtkTreeView         *view,
                    GtkTreePath         *path,
                    GtkTreeViewColumn   *column,
                    gpointer            data)
{
  GtkTreeIter iter;
  if (gtk_tree_model_get_iter (GTK_TREE_MODEL (lang_model), &iter, path))
    {
      g_free (dialog_selected_language);
      gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter,
			  LOCALE_COLUMN, &dialog_selected_language,
			  -1);
      gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_OK);
  }
}

static void
greeter_language_setup_treeview (void)
{
  if (dialog == NULL)
    {
      GtkWidget *main_vbox;
      GtkWidget *button;
      GtkWidget **tmp_p;
      GtkWidget *swindow;
      GtkWidget *label;
      char *s;

      dialog = gtk_dialog_new_with_buttons (_("Select a Language"),
#ifdef TODO
					    GTK_WINDOW (parent_window),
#endif
					    NULL,
					    0,
					    GTK_STOCK_CANCEL,
					    GTK_RESPONSE_CANCEL,
					    NULL);
					    
      button = gtk_button_new_with_mnemonic (_("Change _Language"));
      GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
      gtk_widget_show (button);
      gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
                                    GTK_RESPONSE_OK);
					    
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
      gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
      gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);

      main_vbox = gtk_vbox_new (FALSE, 6);
      gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
                          main_vbox, TRUE, TRUE, 0);
  
      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_OK);
      /* evil gcc warnings */
      tmp_p = &dialog;
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *)tmp_p);
      s = g_strdup (_("_Select the language for your session to use:"));
      label = gtk_label_new_with_mnemonic (s);
      gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
      g_free (s);
      gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
      gtk_box_pack_start (GTK_BOX (main_vbox),
			  label, FALSE, FALSE, 0);
      tv = gtk_tree_view_new ();
      gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (tv), TRUE);
      gtk_label_set_mnemonic_widget (GTK_LABEL (label), tv);
      /* FIXME: we should handle this better, but things really look
       * bad if we aren't always LTR */
      gtk_widget_set_direction (tv, GTK_TEXT_DIR_LTR);
      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv), FALSE);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tv),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "text", TRANSLATED_NAME_COLUMN,
					       NULL);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tv),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "markup",
					       UNTRANSLATED_NAME_COLUMN,
					       NULL);
      swindow = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (swindow), GTK_SHADOW_IN);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
      gtk_container_add (GTK_CONTAINER (swindow), tv);
      gtk_box_pack_start (GTK_BOX (main_vbox),
			  swindow, TRUE, TRUE, 0);
      gtk_window_set_default_size (GTK_WINDOW (dialog),
				   MIN (400, gdm_wm_screen.width),
				   MIN (600, gdm_wm_screen.height));
      g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (tv))),
			"changed",
			(GCallback) selection_changed,
			NULL);
      g_signal_connect (G_OBJECT (tv),
                        "row_activated",
                        (GCallback) tree_row_activated,
                        NULL);
      gtk_tree_view_set_model (GTK_TREE_VIEW (tv),
			       GTK_TREE_MODEL (lang_model));
    }
}

void
greeter_language_set (char *language)
{
   char *locale;
   GtkTreeIter iter = {0};

   g_free (current_language);
   current_language = g_strdup (language);

   if (dialog == NULL)
     greeter_language_setup_treeview ();

   if (language == NULL)
      return;
 
   greeter_custom_set_language (language);

   GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
   gtk_tree_selection_unselect_all (selection);

   if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (lang_model), &iter)) {
      do {
         gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter, LOCALE_COLUMN, &locale, -1);
         if (locale != NULL && strcmp (locale, language) == 0) {
            GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (lang_model), &iter);

            gtk_tree_selection_select_iter (selection, &iter);
            gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv), path, NULL, FALSE, 0.0, 0.0);
            gtk_tree_path_free (path);
            break;
         }
      } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (lang_model), &iter));
   }
}

/*
 * The button with this handler appears in the F10 menu, so it
 * cannot depend on callback data being passed in.
 */
void
greeter_language_handler (GreeterItemInfo *info, gpointer user_data)
{
  if (dialog == NULL)
    greeter_language_setup_treeview ();

  gtk_widget_show_all (dialog);
  gdm_wm_center_window (GTK_WINDOW (dialog));

  gdm_wm_no_login_focus_push ();
  if (tv != NULL)
    {
      GtkTreeSelection *selection;
	  
      gtk_widget_show_now (dialog);
      selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
      if (selection == NULL)
	gtk_tree_selection_select_path (selection, gtk_tree_path_new_first ());
      else
        {
          GtkTreeIter iter;
          GtkTreePath *path;
          GtkTreeModel *tm = GTK_TREE_MODEL (lang_model);

          gtk_tree_selection_get_selected (selection, &tm, &iter);
          path = gtk_tree_model_get_path (GTK_TREE_MODEL (lang_model), &iter);
          gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (tv), path, NULL, FALSE, 0.0, 0.0);
          gtk_tree_path_free (path);
        }
    }
  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_OK:
      if (dialog_selected_language)
        greeter_language_set ((char *) dialog_selected_language);
      break;
    case GTK_RESPONSE_CANCEL:
    default:
      break;
    }

  gdm_wm_no_login_focus_pop ();

  if (dialog)
    gtk_widget_hide (dialog);
}

