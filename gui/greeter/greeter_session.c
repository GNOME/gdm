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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gdm.h"
#include "gdmwm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "gdmsession.h"

#include "gdm-common.h"

#include "greeter.h"
#include "greeter_session.h"
#include "greeter_item_pam.h"
#include "greeter_item_customlist.h"
#include "greeter_configuration.h"
#include "greeter_events.h"
#include "greeter_parser.h"

static GtkWidget  *session_dialog;
static GSList     *session_group = NULL;
extern GList      *sessions;
extern GHashTable *sessnames;
extern gchar      *default_session;
extern char       *current_session;
extern gboolean    session_dir_whacked_out;

void
greeter_set_session (char *session)
{
   g_free (current_session);
   current_session = g_strdup (session);
   greeter_custom_set_session (session);
}

void 
greeter_session_init (void)
{
  GtkWidget *w = NULL;
  GtkWidget *hbox = NULL;
  GtkWidget *main_vbox = NULL;
  GtkWidget *vbox = NULL;
  GtkWidget *cat_vbox = NULL;
  GtkWidget *radio;
  GtkWidget *dialog;
  GtkWidget *button;
  GList *tmp;
  static GtkTooltips *tooltips = NULL;
  GtkRequisition req;
  char *s;
  int num = 1;
  char *label;

  greeter_set_session (NULL);
  
  session_dialog = dialog = gtk_dialog_new ();
  if (tooltips == NULL)
	  tooltips = gtk_tooltips_new ();

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);

  button = gtk_button_new_with_mnemonic (_("Change _Session"));
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_widget_show (button);
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
                                GTK_RESPONSE_OK);

  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
  gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dialog)->vbox), 2);
  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
  gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

  main_vbox = gtk_vbox_new (FALSE, 18);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      main_vbox,
		      FALSE, FALSE, 0);

  cat_vbox = gtk_vbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox),
		      cat_vbox,
		      FALSE, FALSE, 0);

  s = g_strdup_printf ("<b>%s</b>", _("Sessions"));
  w = gtk_label_new (s);
  gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
  g_free (s);
  gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (cat_vbox), w, FALSE, FALSE, 0);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (cat_vbox),
		      hbox, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox),
		      gtk_label_new ("    "),
		      FALSE, FALSE, 0);
  vbox = gtk_vbox_new (FALSE, 6);
  /* we will pack this later depending on size */

  if (gdm_config_get_bool (GDM_KEY_SHOW_LAST_SESSION))
    {
      greeter_set_session (LAST_SESSION);

      radio = gtk_radio_button_new_with_mnemonic (session_group, _("_Last session"));
      g_object_set_data (G_OBJECT (radio),
			 SESSION_NAME,
			 LAST_SESSION);
      session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
      gtk_tooltips_set_tip (tooltips, radio,
			    _("Log in using the session that you have used "
			      "last time you logged in"),
			    NULL);
      gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
      gtk_widget_show (radio);
    }

    gdm_session_list_init ();

    for (tmp = sessions; tmp != NULL; tmp = tmp->next)
      {
	GdmSession *session;
	char *file;
 
	file = (char *) tmp->data;
	session = g_hash_table_lookup (sessnames, file);
 
	if (num < 10 &&
	   (strcmp (file, GDM_SESSION_FAILSAFE_GNOME) != 0) &&
	   (strcmp (file, GDM_SESSION_FAILSAFE_XTERM) != 0))
		label = g_strdup_printf ("_%d. %s", num, session->name);
	else
		label = g_strdup (session->name);
	num++;
 
	radio = gtk_radio_button_new_with_mnemonic (session_group, label);
	g_free (label);
	g_object_set_data_full (G_OBJECT (radio), SESSION_NAME,
		file, (GDestroyNotify) g_free);
	session_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (radio));
	gtk_box_pack_start (GTK_BOX (vbox), radio, FALSE, FALSE, 0);
	gtk_widget_show (radio);
 
	if (! ve_string_empty (session->comment))
		gtk_tooltips_set_tip
			(tooltips, GTK_WIDGET (radio), session->comment, NULL);
     }
                    
   gtk_widget_show_all (vbox);
   gtk_widget_size_request (vbox, &req);

   /* if too large */
   if (req.height > 0.7 * gdm_wm_screen.height) {
	    GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
	    gtk_widget_set_size_request (sw,
					 req.width, 
					 0.7 * gdm_wm_screen.height);
	    gtk_scrolled_window_set_shadow_type
		    (GTK_SCROLLED_WINDOW (sw),
		     GTK_SHADOW_NONE);
	    gtk_scrolled_window_set_policy
		    (GTK_SCROLLED_WINDOW (sw),
		     GTK_POLICY_NEVER,
		     GTK_POLICY_AUTOMATIC);
	    gtk_scrolled_window_add_with_viewport
		    (GTK_SCROLLED_WINDOW (sw), vbox);
	    gtk_widget_show (sw);
	    gtk_box_pack_start (GTK_BOX (hbox),
				sw,
				TRUE, TRUE, 0);
   } else {
	    gtk_box_pack_start (GTK_BOX (hbox),
				vbox,
				TRUE, TRUE, 0);
   }
}

/* 
 * The button with this handler appears in the F10 menu, so it
 * cannot depend on callback data being passed in.
 */
static void
greeter_session_handler (GreeterItemInfo *info,
			 gpointer         user_data)
{
  GSList *tmp;
  int ret;

  /* Select the proper session */
  tmp = session_group;
  while (tmp != NULL)
    {
      GtkWidget *w = tmp->data;
      const char *n;
      
      n = g_object_get_data (G_OBJECT (w), SESSION_NAME);
      
      if (n && strcmp (n, current_session) == 0)
	{
	  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w),
					TRUE);
	  break;
	}
      
      tmp = tmp->next;
    }

  gtk_widget_show_all (session_dialog);

  gdm_wm_center_window (GTK_WINDOW (session_dialog));
  
  gdm_wm_no_login_focus_push ();
  ret = gtk_dialog_run (GTK_DIALOG (session_dialog));
  gdm_wm_no_login_focus_pop ();
  gtk_widget_hide (session_dialog);

  if (ret == GTK_RESPONSE_OK)
    {
      tmp = session_group;
      while (tmp != NULL)
	{
	  GtkWidget *w = tmp->data;
	  const char *n;
	  
	  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (w)))
	    {
	      n = g_object_get_data (G_OBJECT (w), SESSION_NAME);
              greeter_set_session ((char *)n);
	      break;
	    }
	  
	  tmp = tmp->next;
	}
    }
}

void
greeter_item_session_setup ()
{
  greeter_item_register_action_callback ("session_button",
					 (ActionFunc)greeter_session_handler,
					 NULL);
}
