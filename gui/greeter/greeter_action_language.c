#include "config.h"

#include <libgnome/libgnome.h>
#include <gtk/gtk.h>
#include <string.h>

#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmlanguages.h"
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_item_pam.h"
#include "greeter_action_language.h"

#define LAST_LANGUAGE "Last"
#define DEFAULT_LANGUAGE "Default"

enum {
  LOCALE_COLUMN,
  TRANSLATED_NAME_COLUMN,
  UNTRANSLATED_NAME_COLUMN,
  NUM_COLUMNS
};

static GtkListStore *lang_model = NULL;
static GtkWidget *dialog = NULL;
static gboolean savelang = FALSE;
static gchar *current_language = NULL;
static gchar *dialog_selected_language = NULL;

static void
greeter_langauge_initialize_model (void)
{
  GList *list, *li;
  GtkTreeIter iter;

  list = gdm_lang_read_locale_file (GdmLocaleFile);

  lang_model = gtk_list_store_new (NUM_COLUMNS,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING);

  gtk_list_store_append (lang_model, &iter);
  gtk_list_store_set (lang_model, &iter,
		      TRANSLATED_NAME_COLUMN, _("Last"),
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

gboolean
greeter_language_get_save_language (void)
{
  return savelang;
}

gchar *
greeter_language_get_language (const char *old_language)
{
  gchar *retval = NULL;

  /* Don't save language unless told otherwise */
  savelang = FALSE;

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
	  gchar *msg;
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

	  msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is "
				   "%s.\nDo you wish to make %s the default for future sessions?"),
				 current_name, saved_name, current_name);
	  g_free (current_name);
	  g_free (saved_name);

	  savelang = gdm_common_query (msg, TRUE /* markup */, _("Make _Default"), _("Just For _This Session"));
	  g_free (msg);
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

void
greeter_action_language (GreeterItemInfo *info,
			 gpointer         user_data)
{
  GtkWidget *view = NULL;

  if (dialog == NULL)
    {
      GtkWidget **tmp_p;
      GtkWidget *swindow;
      GtkWidget *label;
      char *s;

      dialog = gtk_dialog_new_with_buttons (_("Select a language"),
#if TODO
					    GTK_WINDOW (parent_window),
#endif
					    NULL,
					    0,
					    GTK_STOCK_CANCEL,
					    GTK_RESPONSE_CANCEL,
					    GTK_STOCK_OK,
					    GTK_RESPONSE_OK,
					    NULL);
      gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

      gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				       GTK_RESPONSE_OK);
      /* evil gcc warnings */
      tmp_p = &dialog;
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *)tmp_p);
      s = g_strdup_printf ("<span size=\"x-large\" weight=\"bold\">%s</span>",
			   _("Select a language for your session to use:"));
      label = gtk_label_new (s);
      g_free (s);
      gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			  label, FALSE, FALSE, 0);
      view = gtk_tree_view_new ();
      gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);
      /* FIXME: we should handle this better, but things really look
       * like crap if we aren't always LTR */
      gtk_widget_set_direction (view, GTK_TEXT_DIR_LTR);
      gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "text", TRANSLATED_NAME_COLUMN,
					       NULL);
      gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "markup",
					       UNTRANSLATED_NAME_COLUMN,
					       NULL);
      swindow = gtk_scrolled_window_new (NULL, NULL);
      gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
      gtk_container_add (GTK_CONTAINER (swindow), view);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			  swindow, TRUE, TRUE, 0);
      gtk_window_set_default_size (GTK_WINDOW (dialog),
				   MIN (400, gdm_wm_screen.width),
				   MIN (600, gdm_wm_screen.height));
      g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (view))),
			"changed",
			(GCallback) selection_changed,
			NULL);
      g_signal_connect(G_OBJECT (view),
                        "row_activated",
                        (GCallback) tree_row_activated,
                        NULL);
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
    }
  gdm_wm_no_login_focus_push ();
  if (view != NULL)
    {
      gtk_widget_show_now (dialog);
      greeter_langauge_initialize_model ();
      gtk_tree_view_set_model (GTK_TREE_VIEW (view),
			       GTK_TREE_MODEL (lang_model));
    }
  switch (gtk_dialog_run (GTK_DIALOG (dialog)))
    {
    case GTK_RESPONSE_OK:
      if (dialog_selected_language)
	current_language = g_strdup (dialog_selected_language);
      break;
    case GTK_RESPONSE_CANCEL:
    default:
      break;
    }

  gdm_wm_no_login_focus_pop ();

  if (dialog)
    gtk_widget_hide (dialog);
}

