#include "greeter_action_language.h"
#include <gtk/gtk.h>

enum {
  NAME_COLUMN,
  LOCALE_COLUMN,
  TRANSLATED_NAME_COLUMN,
  NUM_COLUMNS
};

typedef struct _Language Language;
struct _Language {
	char *name;
	char *locale;
};

#include "greeter_lang_list.c"

static GtkListStore *lang_model = NULL;
static GtkWidget *dialog = NULL;

static void
greeter_lang_dialog_init (GtkWidget *parent_window)
{
  gint i;
  GtkWidget *label;
  GtkWidget *view;
  GtkWidget *swindow;

  if (lang_model != NULL)
    return;

  lang_model = gtk_list_store_new (NUM_COLUMNS,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING);

  for (i = 0; languages[i].name != NULL; i++)
    {
      GtkTreeIter iter;

      gtk_list_store_append (lang_model, &iter);
      gtk_list_store_set (lang_model, &iter,
			  NAME_COLUMN, languages[i].name,
			  LOCALE_COLUMN, languages[i].locale,
			  TRANSLATED_NAME_COLUMN, _(languages[i].name),
			  -1);
    }

  dialog = gtk_dialog_new_with_buttons ("Select a language",
					GTK_WINDOW (parent_window),
					0,
					GTK_STOCK_OK,
					GTK_RESPONSE_OK,
					GTK_STOCK_CANCEL,
					GTK_RESPONSE_CANCEL,
					NULL);

  label = gtk_label_new ("<span size=\"x-large\" weight=\"bold\">Select a language for your session to use:</span>");
  gtk_label_set_use_markup (GTK_LABEL (label), TRUE);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      label, FALSE, FALSE, 0);
  view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (lang_model));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view),
					       GTK_DIALOG_MODAL,
					       NULL,
					       gtk_cell_renderer_text_new (),
					       "text", TRANSLATED_NAME_COLUMN,
					       NULL);
  swindow = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (swindow), view);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      swindow, TRUE, TRUE, 0);
  gtk_widget_show_all (GTK_DIALOG (dialog)->vbox);

  gtk_window_set_default_size (GTK_WINDOW (dialog),
			       -1,
			       gdk_screen_height ()/2);
}


void
greeter_action_language (GreeterItemInfo *info,
			 gpointer         user_data)
{
  greeter_lang_dialog_init (GTK_WIDGET (user_data));

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_hide (dialog);
}
