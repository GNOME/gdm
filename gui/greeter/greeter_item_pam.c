#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "greeter_item_pam.h"
#include "greeter_parser.h"
#include "gdm.h"
#include "vicious.h"
#include <string.h>

static gboolean messages_to_give = FALSE;
static gboolean replace_msg = TRUE;
static guint err_box_clear_handler = 0;
static gboolean entry_is_login = FALSE;

gchar *greeter_current_user = NULL;

void
greeter_item_pam_set_user (const char *user)
{
  g_free (greeter_current_user);
  greeter_current_user = g_strdup (user);
}

static void
user_pw_activate (GtkEntry *entry, GreeterItemInfo *info)
{
  static gboolean first_return = TRUE;
  char *tmp;
  GreeterItemInfo *error_info;
  GreeterItemInfo *message_info;
  
  gtk_widget_set_sensitive (GTK_WIDGET (entry), FALSE);

  /* Save login. I'm making the assumption that login is always
   * the first thing entered. This might not be true for all PAM
   * setups. Needs thinking! 
   */
  
  if (entry_is_login) {
    g_free (greeter_current_user);
    greeter_current_user = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
  }
  
  /* somewhat ugly thing to clear the initial message */
  if (first_return) {
    first_return = FALSE;
    message_info = greeter_lookup_id ("pam-message");
    if (message_info)
      g_object_set (G_OBJECT (message_info->item),
		    "text", "",
		    NULL);
  }
  
  /* clear the err_box */
  if (err_box_clear_handler > 0)
    {
      g_source_remove (err_box_clear_handler);
      err_box_clear_handler = 0;
    }
  error_info = greeter_lookup_id ("pam-error");
  if (error_info)
    g_object_set (G_OBJECT (error_info->item),
		  "text", "",
		  NULL);
  
  entry_is_login = FALSE;

  tmp = ve_locale_from_utf8 (gtk_entry_get_text (GTK_ENTRY (entry)));
  printf ("%c%s\n", STX, tmp);
  fflush (stdout);
  g_free (tmp);
}

gboolean
greeter_item_pam_setup (void)
{
  GreeterItemInfo *entry_info;

  entry_info = greeter_lookup_id ("user-pw-entry");
  if (entry_info && entry_info->item &&
      GNOME_IS_CANVAS_WIDGET (entry_info->item))
    {
      GtkWidget *entry;
      entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
      gtk_widget_grab_focus (entry);

      /* hack.  For some reason if we leave it blank,
       * we'll get a little bit of activity on first keystroke,
       * this way we get rid of it, it will be cleared for the
       * first prompt anyway. */
      gtk_entry_set_text (GTK_ENTRY (entry), "...");

      /* initially insensitive */
      gtk_widget_set_sensitive (entry, FALSE);

      g_signal_connect (entry, "activate",
			GTK_SIGNAL_FUNC (user_pw_activate), entry_info);
    }
  return TRUE;
}


void
greeter_item_pam_prompt (const char *message,
			 int         entry_len,
			 gboolean    entry_visible,
			 gboolean    is_login)
{
  GreeterItemInfo *conversation_info;
  GreeterItemInfo *entry_info;
  GtkWidget *entry;

  entry_is_login = is_login;
  
  conversation_info = greeter_lookup_id ("pam-prompt");
  entry_info = greeter_lookup_id ("user-pw-entry");

  if (conversation_info)
    g_object_set (G_OBJECT (conversation_info->item),
		  "text", message,
		  NULL);
  
  if (entry_info && entry_info->item &&
      GNOME_IS_CANVAS_WIDGET (entry_info->item))
    {
      entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
      
      gtk_entry_set_visibility (GTK_ENTRY (entry), entry_visible);
      gtk_widget_set_sensitive (GTK_WIDGET (entry), TRUE);
      gtk_entry_set_max_length (GTK_ENTRY (entry), entry_len);
      gtk_entry_set_text (GTK_ENTRY (entry), "");
      gtk_widget_grab_focus (entry);
    }

  messages_to_give = FALSE;
  replace_msg = TRUE;
}

void
greeter_item_pam_message (const char *message)
{
  GreeterItemInfo *message_info;
  char *oldtext;
  char *newtext;
  
  /* the user has not yet seen messages */
  messages_to_give = TRUE;

  message_info = greeter_lookup_id ("pam-message");

  if (message_info)
    {
      /* HAAAAAAACK.  Sometimes pam sends many many messages, SO
       * we try to collect them until the next prompt or reset or
       * whatnot */
      if (!replace_msg)
	{
	  g_object_get (G_OBJECT (message_info->item),
			"text", &oldtext,
			NULL);
	  if (strlen (oldtext) > 0)
	    {
	      newtext = g_strdup_printf ("%s\n%s", oldtext, message);
	      g_object_set (G_OBJECT (message_info->item),
			    "text", newtext,
			    NULL);
	      g_free (newtext);
	    }
	  else
	    g_object_set (G_OBJECT (message_info->item),
			  "text", message,
			  NULL);
	}
      else
	g_object_set (G_OBJECT (message_info->item),
		      "text", message,
		      NULL);
    }
  replace_msg = FALSE;
}


static gboolean
error_clear (gpointer data)
{
	GreeterItemInfo *error_info = data;

	g_object_set (G_OBJECT (error_info->item),
		      "text", "",
		      NULL);

	err_box_clear_handler = 0;
	return FALSE;
}

static char *
break_at (const char *orig, int len)
{
  char *text, *p, *newline;
  int i;

  text = g_strdup (orig);
  
  p = text;
  
  while (strlen (p) > len)
    {
      newline = strchr (p, '\n');
      if (newline &&
	  ((newline - p) < len))
	{
	  p = newline + 1;
	  continue;
	}

      for (i = MIN (len, strlen (p)-1); i > 0; i--)
	{
	  if (p[i] == ' ')
	    {
	      p[i] = '\n';
	      p = p + i + 1;
	      goto out;
	    }
	}
      /* No place to break. give up */
      p = p + 80;
    out:
    }
  return text;
}

void
greeter_item_pam_error (const char *message)
{
  GreeterItemInfo *error_info;
  char *text;

  /* The message I got from pam had a silly newline
   * in the beginning. That may make sense for a
   * terminal based conversation, but it sucks for
   * us, so skip it.
   */
  if (message[0] == '\n')
    message++;
  
  error_info = greeter_lookup_id ("pam-error");
  if (error_info)
    {
      /* This is a bad hack that I added because
       * the canvas text item doesn't support
       * text wrapping...
       */
      text = break_at (message, 50);

      g_object_set (G_OBJECT (error_info->item),
		    "text", text,
		    NULL);
      g_free (text);
      
      if (err_box_clear_handler > 0)
	g_source_remove (err_box_clear_handler);
      if (strlen (message) == 0)
	err_box_clear_handler = 0;
      else
	err_box_clear_handler = g_timeout_add (30000,
					       error_clear,
					       error_info);
    }
}

void
greeter_item_pam_leftover_messages (void)
{
  if (messages_to_give)
    {
      char *oldtext = NULL;
      GreeterItemInfo *message_info;

      message_info = greeter_lookup_id ("pam-message");

      if (message_info != NULL)
        {
	  g_object_get (G_OBJECT (message_info->item),
			"text", &oldtext,
			NULL);
	}

      if ( ! ve_string_empty (oldtext))
        {
	  GtkWidget *dlg;

	  /* we should be now fine for focusing new windows */
	  gdm_wm_focus_new_windows (TRUE);

	  dlg = gtk_message_dialog_new (NULL /* parent */,
					GTK_DIALOG_MODAL /* flags */,
					GTK_MESSAGE_INFO,
					GTK_BUTTONS_OK,
					"%s",
					oldtext);
	  gtk_window_set_modal (GTK_WINDOW (dlg), TRUE);
	  gdm_wm_center_window (GTK_WINDOW (dlg));

	  gdm_wm_no_login_focus_push ();
	  gtk_dialog_run (GTK_DIALOG (dlg));
	  gtk_widget_destroy (dlg);
	  gdm_wm_no_login_focus_pop ();
	}
      messages_to_give = FALSE;
    }
}

