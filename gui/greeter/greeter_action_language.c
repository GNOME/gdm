#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "gdmwm.h"
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_item_pam.h"
#include "greeter_action_language.h"

#define LAST_LANGUAGE "Last"

enum {
  NAME_COLUMN,
  LOCALE_COLUMN,
  TRANSLATED_NAME_COLUMN,
  ENCODING_COLUMN,
  FOUND_COLUMN,
  NUM_COLUMNS
};

typedef struct _Language Language;
#include "greeter_lang_list.c"


static GtkListStore *lang_model = NULL;
static GtkWidget *dialog = NULL;
static gboolean savelang = FALSE;
static gchar *current_language = NULL;
static gchar *dialog_selected_language = NULL;

static gboolean
greeter_language_find_lang (const char  *language,
			    GtkTreeIter *iter)
{
  return FALSE;
}     

static gchar *
greeter_language_get_name (const char *locale,
			   const char *language,
			   gboolean    never_encoding)
{
  char *name = NULL;
  GtkTreeIter iter;
  const char *encoding;

  /* FIXME: we need to get the languages for right locales here,
   * for now we just translate the current locale */
  if (locale != NULL)
    return NULL;

  if (! greeter_language_find_lang (language, &iter))
    return g_strdup (language);

  encoding = strchr (language, '.');
  if (encoding != NULL)
    encoding++;

  /* if more then one language in the language file with this
   * locale, then hell, include the encoding to differentiate them */
#if TODO
  if (lang->found > 1 &&
      encoding != NULL &&
      ! never_encoding)
    name = g_strdup_printf ("%s (%s)", _(lang->name), encoding);
  else
    name = g_strdup (_(lang->name));
#endif
  
  return name;
}


static void
greeter_langauge_initialize_model (void)
{
  gint i;

  lang_model = gtk_list_store_new (NUM_COLUMNS,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_STRING,
				   G_TYPE_INT);

  for (i = 0; languages[i].name != NULL; i++)
    {
      GtkTreeIter iter;
      gchar *full_name;
      gchar *free_me = NULL;
      gtk_list_store_append (lang_model, &iter);
      if (languages[i].untranslated_name != NULL)
	{
	  full_name = g_strdup_printf ("%s (%s)", _(languages[i].name), languages[i].untranslated_name);
	  free_me = full_name;
	}
      else
	full_name = _(languages[i].name);
      gtk_list_store_set (lang_model, &iter,
			  NAME_COLUMN, languages[i].name,
			  LOCALE_COLUMN, languages[i].locale,
			  TRANSLATED_NAME_COLUMN, full_name,
			  FOUND_COLUMN, 0,
			  -1);
      g_free (free_me);
    }
}

void
greeter_language_init (void)
{
  static gboolean initted = FALSE;

  g_assert (initted == FALSE);
    
  greeter_langauge_initialize_model ();


  initted = TRUE;
}

gboolean
greeter_language_get_save_language (void)
{
  return FALSE;
}

gchar *
greeter_language_get_language (const char *old_language)
{
  gchar *retval = NULL;

#if 0
  if (greeter_current_user == NULL)
    greeter_abort ("greeter_language_get_language: curuser==NULL. Mail <mkp@mkp.net> with " \
		    "information on your PAM and user database setup");
#endif
  
  /* Don't save language unless told otherwise */
  savelang = FALSE;

  /* Previously saved language not found in ~user/.gnome/gdm */
  if (old_language == NULL || old_language[0] == '\000')
    {
      /* If "Last" is chosen use Default, which is the current language,
       * or the GdmDefaultLocale if that's not set or is "C"
       * else use current selection */

      if (current_language == NULL ||
	  strcmp (current_language, LAST_LANGUAGE) == 0)
	{
	  const char *lang = g_getenv ("LANG");
	  if (lang == NULL || lang[0] == '\000' ||
	      g_ascii_strcasecmp (lang, "C") == 0)
	    {
	      retval = g_strdup (GdmDefaultLocale);
	    }
	  else
	    {
	      retval = g_strdup (lang);
	    }
	}
      else
	{
	  retval = g_strdup (current_language);
	}

      if (retval == NULL)
	{
	  retval= g_strdup ("C");
	}

	savelang = TRUE;

	return retval;
    }

  /* If a different language is selected */
  if (current_language != NULL && strcmp (current_language, LAST_LANGUAGE) != 0)
    {
      retval = g_strdup (current_language);

      /* User's saved language is not the chosen one */
      if (strcmp (old_language, retval) != 0)
	{
	  gchar *msg;
	  char *current_name, *saved_name;

	  current_name = greeter_language_get_name (NULL, current_language, FALSE);
	  saved_name = greeter_language_get_name (NULL, old_language, FALSE);

	  msg = g_strdup_printf (_("You have chosen %s for this session, but your default setting is "
				   "%s.\nDo you wish to make %s the default for future sessions?"),
				 current_name, saved_name, current_name);
	  g_free (current_name);
	  g_free (saved_name);

#if TODO
	  savelang = gdm_login_query (msg);
#endif
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

  gtk_tree_selection_get_selected (selection, NULL, &iter);
  g_free (dialog_selected_language);
  gtk_tree_model_get (GTK_TREE_MODEL (lang_model), &iter, LOCALE_COLUMN, &dialog_selected_language, -1);
}

void
greeter_action_language (GreeterItemInfo *info,
			 gpointer         user_data)
{
  if (dialog == NULL)
    {
      GtkWidget *view;
      GtkWidget *swindow;
      GtkWidget *label;

      dialog = gtk_dialog_new_with_buttons ("Select a language",
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
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
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
      gtk_window_set_default_size (GTK_WINDOW (dialog),
				   -1,
				   gdm_wm_screen.height);
      g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (view))),
			"changed",
			(GCallback) selection_changed,
			NULL);
      gtk_widget_show_all (dialog);
      gdm_wm_center_window (GTK_WINDOW (dialog));
    }
  gdm_wm_no_login_focus_push ();
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

