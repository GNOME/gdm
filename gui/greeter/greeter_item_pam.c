#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "greeter_item_pam.h"
#include "greeter_parser.h"
#include "gdm.h"
#include <string.h>


gboolean got_username = FALSE;
char *username = NULL;

static void
user_pw_activate (GtkEntry *entry, GreeterItemInfo *info)
{
  GreeterItemInfo *pam_info;
  const char *tmp;
  const char *password;

  g_print ("%c%s\n", STX, gtk_entry_get_text (GTK_ENTRY (entry)));

#if 0
  pam_info = greeter_lookup_id ("pam-conversation");
  
  if (!got_username)
    {
      tmp = gtk_entry_get_text (entry);
      if (*tmp == 0)
	return;
      username = g_strdup (tmp);

      gtk_entry_set_visibility (entry, FALSE);
      gtk_entry_set_text (entry, "");
      got_username = TRUE;

      if (pam_info && pam_info->item &&
	  GNOME_IS_CANVAS_TEXT (pam_info->item))
	{
	  g_object_set (G_OBJECT (pam_info->item),
			"text",	"Please enter password",
			NULL);
	}
    }
  else
    {
      password = gtk_entry_get_text (entry);

      if (strcmp (password, "foobar") == 0)
	{
	  exit (0);
	}
      else
	{
	  greeter_item_update_text (pam_info);
	  gtk_entry_set_visibility (entry, TRUE);
	  gtk_entry_set_text (entry, "");
	}

      got_username = FALSE;
      g_free (username);
    }
#endif
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
