#include "config.h"

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include <gdk/gdkkeysyms.h>
#include "greeter_item_pam.h"
#include "greeter_parser.h"
#include "gdm.h"
#include "gdmwm.h"
#include "vicious.h"
#include <string.h>

static gboolean messages_to_give = FALSE;
static gboolean replace_msg = TRUE;
static guint err_box_clear_handler = 0;
static gboolean entry_is_login = FALSE;

extern gboolean DOING_GDM_DEVELOPMENT;
extern gboolean GdmSystemMenu;

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
  char *tmp;
  GreeterItemInfo *error_info;
  
  gtk_widget_set_sensitive (GTK_WIDGET (entry), FALSE);

  /* Save login. I'm making the assumption that login is always
   * the first thing entered. This might not be true for all PAM
   * setups. Needs thinking! 
   */
  
  if (entry_is_login) {
    g_free (greeter_current_user);
    greeter_current_user = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
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

static gboolean
key_press_event (GtkWidget *entry, GdkEventKey *event, gpointer data)
{
  if ((event->keyval == GDK_Tab ||
       event->keyval == GDK_KP_Tab) &&
      (event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) == 0)
    {
      g_signal_emit_by_name (entry,
		             "insert_at_cursor",
		             "\t");
      return TRUE;
  }

  return FALSE;
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

      if ( ! DOING_GDM_DEVELOPMENT)
        {
          /* hack.  For some reason if we leave it blank,
           * we'll get a little bit of activity on first keystroke,
           * this way we get rid of it, it will be cleared for the
           * first prompt anyway. */
          gtk_entry_set_text (GTK_ENTRY (entry), "...");

          /* initially insensitive */
          gtk_widget_set_sensitive (entry, FALSE);
	}

      g_signal_connect (entry, "activate",
			G_CALLBACK (user_pw_activate), entry_info);
      g_signal_connect (G_OBJECT (entry), "key_press_event",
		        G_CALLBACK (key_press_event), NULL);
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
break_at (const char *orig, int columns)
{
  PangoLogAttr *attrs;
  int n_chars;
  GString *str;
  int i;
  int in_current_row;
  const char *p;
  
  str = g_string_new (NULL);
  n_chars = g_utf8_strlen (orig, -1);

  attrs = g_new0 (PangoLogAttr, n_chars + 1);
  pango_get_log_attrs (orig, -1,
                       0, gtk_get_default_language (),
                       attrs, n_chars + 1);  

  in_current_row = 0;
  i = 0;
  p = orig;
  while (i < n_chars)
    {
      gunichar ch;

      ch = g_utf8_get_char (p);
      
      /* Broken algorithm for simplicity; we just break
       * at the first place we can within 10 of the end
       */
      
      if (in_current_row > (columns - 10) &&
          attrs[i].is_line_break)
        {
          in_current_row = 0;
          g_string_append_unichar (str, '\n');
        }

      ++in_current_row;
      g_string_append_unichar (str, ch);
      
      p = g_utf8_next_char (p);
      ++i;
    }
  
  g_free (attrs);

  return g_string_free (str, FALSE);
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

