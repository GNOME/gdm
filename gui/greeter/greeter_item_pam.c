#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "greeter_item_pam.h"
#include "greeter_parser.h"
#include "gdm.h"
#include <string.h>

static gboolean messages_to_give = FALSE;
static gboolean replace_msg = TRUE;
static guint err_box_clear_handler = 0;

static void
user_pw_activate (GtkEntry *entry, GreeterItemInfo *info)
{
  g_print ("%c%s\n", STX, gtk_entry_get_text (GTK_ENTRY (entry)));
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

      g_signal_connect (entry, "activate",
			GTK_SIGNAL_FUNC (user_pw_activate), entry_info);
    }
  return TRUE;
}


void
greeter_item_pam_prompt (const char *message,
			 int         entry_len,
			 gboolean    entry_visible)
{
  GreeterItemInfo *conversation_info;
  GreeterItemInfo *entry_info;
  GtkWidget *entry;
  
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
      
      gtk_widget_grab_focus (entry);
      gtk_entry_set_visibility (GTK_ENTRY (entry), entry_visible);
      gtk_entry_set_max_length (GTK_ENTRY (entry), 32);
      gtk_entry_set_text (GTK_ENTRY (entry), "");
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
error_clear (GreeterItemInfo *error_info)
{
      g_object_set (G_OBJECT (error_info->item),
		    "text", "",
		    NULL);

	err_box_clear_handler = 0;
	return FALSE;
}

void
greeter_item_pam_error (const char *message)
{
  GreeterItemInfo *error_info;
  
  error_info = greeter_lookup_id ("pam-error");
  if (error_info)
    {
      g_object_set (G_OBJECT (error_info->item),
		    "text", message,
		    NULL);
      
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
