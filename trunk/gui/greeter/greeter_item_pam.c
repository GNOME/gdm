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

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

#include "viciousui.h"

#include "greeter.h"
#include "greeter_item.h"
#include "greeter_item_pam.h"
#include "greeter_item_ulist.h"
#include "greeter_item_timed.h"
#include "greeter_parser.h"
#include "greeter_configuration.h"
#include "greeter_canvas_item.h"
#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"

static gboolean messages_to_give = FALSE;
static gboolean replace_msg = TRUE;
static guint err_box_clear_handler = 0;

gchar *greeter_current_user = NULL;

gboolean require_quarter = FALSE;

extern gboolean greeter_probably_login_prompt;
extern GtkButton *gtk_ok_button;
extern GtkButton *gtk_start_again_button;

static gboolean
greeter_item_pam_error_set (gboolean display)
{
  GreeterItemInfo *info;
  GnomeCanvasItem *item;

  info = greeter_lookup_id ("pam-error-logo");

  if (info)
    {
      if (info->group_item != NULL)
        item = GNOME_CANVAS_ITEM (info->group_item);
      else
        item = info->item;

      if (display)
          gnome_canvas_item_show (item);
      else
          gnome_canvas_item_hide (item);
    }

  return TRUE;
}

void
greeter_item_pam_set_user (const char *user)
{
  g_free (greeter_current_user);
  greeter_current_user = g_strdup (user);
  greeter_item_ulist_set_user (user);
}

static gboolean
evil (GtkEntry *entry, const char *user)
{
	/* do not translate */
	if (strcmp (user, "Gimme Random Cursor") == 0) {
		gdm_common_setup_cursor (((rand () >> 3) % (GDK_LAST_CURSOR/2)) * 2);
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
		/* do not translate */
	} else if (strcmp (user, "Require Quater") == 0 ||
		   strcmp (user, "Require Quarter") == 0) {
		/* btw, note that I misspelled quarter before and
		 * thus this checks for Quater as well as Quarter to
		 * keep compatibility which is obviously important for
		 * something like this */
		require_quarter = TRUE;
		gtk_entry_set_text (GTK_ENTRY (entry), "");
		return TRUE;
	}

	return FALSE;
}

static void
set_text (GreeterItemInfo *info, const char *text)
{
	greeter_canvas_item_break_set_string (info,
					      text,
					      FALSE /* markup */,
					      info->data.text.real_max_width,
					      NULL /* width */,
					      NULL /* height */,
					      NULL /* canvas */,
					      info->item);
}

void
greeter_item_pam_login (GtkEntry *entry, GreeterItemInfo *info)
{
  const char *str;
  char *tmp;
  GreeterItemInfo *error_info;

  if (gtk_ok_button != NULL)
	  gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);
  if (gtk_start_again_button != NULL)
	  gtk_widget_set_sensitive (GTK_WIDGET (gtk_start_again_button), FALSE);

  greeter_ignore_buttons (TRUE);

  str = gtk_entry_get_text (GTK_ENTRY (entry));
  if (greeter_probably_login_prompt &&
      /* evilness */
      evil (entry, str))
    {
      /* obviously being 100% reliable is not an issue for
         this test */
      gtk_entry_set_text (GTK_ENTRY (entry), "");
      return;
    }

  if (greeter_probably_login_prompt &&
      ve_string_empty (str) &&
      greeter_item_timed_is_timed ())
    {
      /* timed interruption */
      printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_TIMED_LOGIN);
      fflush (stdout);
      return;
    }
  
  gtk_widget_set_sensitive (GTK_WIDGET (entry), FALSE);

  /* clear the err_box */
  if (err_box_clear_handler > 0)
    {
      g_source_remove (err_box_clear_handler);
      err_box_clear_handler = 0;
    }
  error_info = greeter_lookup_id ("pam-error");
  if (error_info) {
    greeter_item_pam_error_set (FALSE);
    set_text (error_info, "");
  }
  
  tmp = ve_locale_from_utf8 (str);
  printf ("%c%s\n", STX, tmp);
  fflush (stdout);
  g_free (tmp);
}

static gboolean
pam_key_release_event (GtkWidget *entry, GdkEventKey *event, gpointer data)
{
  GreeterItemInfo *entry_info = greeter_lookup_id ("user-pw-entry");

  if (entry_info && entry_info->item &&
      GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
      GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
    {
       const char *login_string;
	GtkWidget *entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;

       if ((event->keyval == GDK_Tab ||
            event->keyval == GDK_KP_Tab) &&
           (event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) == 0)
          {
		greeter_item_pam_login (GTK_ENTRY (entry), entry_info);
		return TRUE;
           }

       if (gtk_ok_button != NULL)
          {
             /*
              * Set ok button to sensitive only if there are characters in
              * the entry field
              */
             login_string = gtk_entry_get_text (GTK_ENTRY (entry));
             if (login_string != NULL && login_string[0] != '\0')
		     gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), TRUE);
             else
		     gtk_widget_set_sensitive (GTK_WIDGET (gtk_ok_button), FALSE);
          }
    }
  return FALSE;
}

gboolean
greeter_item_pam_setup (void)
{
  GreeterItemInfo *entry_info;

  greeter_item_pam_error_set (FALSE);

  entry_info = greeter_lookup_id ("user-pw-entry");
  if (entry_info && entry_info->item &&
      GNOME_IS_CANVAS_WIDGET (entry_info->item) &&
      GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (entry_info->item)->widget))
    {
      GtkWidget *entry = GNOME_CANVAS_WIDGET (entry_info->item)->widget;
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
			G_CALLBACK (greeter_item_pam_login), entry_info);
      g_signal_connect (G_OBJECT (entry), "key_release_event",
		        G_CALLBACK (pam_key_release_event), NULL);
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
    {
      set_text (conversation_info, message);
    }
  
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
      if ( ! replace_msg &&
	   /* empty message is for clearing */
	   ! ve_string_empty (message))
	{
	  g_object_get (G_OBJECT (message_info->item),
			"text", &oldtext,
			NULL);
	  if (strlen (oldtext) > 0)
	    {
	      newtext = g_strdup_printf ("%s\n%s", oldtext, message);
	      set_text (message_info, newtext);
	      g_free (newtext);
	    }
	  else
	    set_text (message_info, message);
	}
      else
        set_text (message_info, message);
    }
  replace_msg = FALSE;
}


static gboolean
error_clear (gpointer data)
{
	GreeterItemInfo *error_info = data;
	err_box_clear_handler       = 0;

        set_text (error_info, "");
        greeter_item_pam_error_set (FALSE);

	return FALSE;
}

void
greeter_item_pam_error (const char *message)
{
  GreeterItemInfo *error_info;

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
      set_text (error_info, message);
      
      if (err_box_clear_handler > 0)
	g_source_remove (err_box_clear_handler);

      if (strlen (message) == 0)
	err_box_clear_handler = 0;
      else
	err_box_clear_handler = g_timeout_add (30000,
					       error_clear,
					       error_info);
      greeter_item_pam_error_set (TRUE);
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

	  dlg = ve_hig_dialog_new (NULL /* parent */,
				   GTK_DIALOG_MODAL /* flags */,
				   GTK_MESSAGE_INFO,
				   GTK_BUTTONS_OK,
				   oldtext,
				   "");
	  gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
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

