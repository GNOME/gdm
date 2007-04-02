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

#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "gdmwm.h"
#include "misc.h"

#include "gdm-common.h"
#include "gdm-socket-protocol.h"
#include "gdm-daemon-config-keys.h"

#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_system.h"
#include "greeter_item.h"
#include "greeter_item_ulist.h"
#include "greeter_parser.h"

GtkWidget       *dialog;
extern gboolean  GdmHaltFound;
extern gboolean  GdmRebootFound;
extern gboolean *GdmCustomCmdsFound;
extern gboolean  GdmAnyCustomCmdsFound;
extern gboolean  GdmSuspendFound;
extern gboolean  GdmConfiguratorFound;

/* doesn't check for executability, just for existance */
static gboolean
bin_exists (const char *command)
{
	char *bin;

	if (ve_string_empty (command))
		return FALSE;

	/* Note, check only for existance, not for executability */
	bin = ve_first_word (command);
	if (bin != NULL &&
	    g_access (bin, F_OK) == 0) {
		g_free (bin);
		return TRUE;
	} else {
		g_free (bin);
		return FALSE;
	}
}

/*
 * The buttons with these handlers appear in the F10 menu, so they
 * cannot depend on callback data being passed in.
 */
static void
query_greeter_restart_handler (void)
{
	if (gdm_wm_warn_dialog (_("Are you sure you want to restart the computer?"), "",
			     _("_Restart"), NULL, TRUE) == GTK_RESPONSE_YES) {
		_exit (DISPLAY_REBOOT);
	}
}

static void
query_greeter_custom_cmd_handler (GtkWidget *widget, gpointer data)
{
        if (data) {
		gint *cmd_id = (gint*)data;
	        gchar * key_string = g_strdup_printf (_("%s%d="), GDM_KEY_CUSTOM_CMD_TEXT_TEMPLATE, *cmd_id);
		if (gdm_wm_warn_dialog (gdm_config_get_string (key_string) , "", 
					GTK_STOCK_OK, NULL, TRUE) == GTK_RESPONSE_YES) {
		        printf ("%c%c%c%d\n", STX, BEL, GDM_INTERRUPT_CUSTOM_CMD, *cmd_id);
			fflush (stdout);
		}
		g_free (key_string);		
	}
}

static void
query_greeter_halt_handler (void)
{
	if (gdm_wm_warn_dialog (_("Are you sure you want to Shut Down the computer?"), "",
			     _("Shut _Down"), NULL, TRUE) == GTK_RESPONSE_YES) {
		_exit (DISPLAY_HALT);
	}
}

static void
query_greeter_suspend_handler (void)
{
	if (gdm_wm_warn_dialog (_("Are you sure you want to suspend the computer?"), "",
			     _("_Suspend"), NULL, TRUE) == GTK_RESPONSE_YES) {
		/* suspend interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_SUSPEND);
		fflush (stdout);
	}
}

static void
greeter_restart_handler (void)
{
	_exit (DISPLAY_REBOOT);
}

static void
greeter_custom_cmd_handler (gint cmd_id)
{
	printf ("%c%c%c%d\n", STX, BEL, GDM_INTERRUPT_CUSTOM_CMD, cmd_id);
	fflush (stdout);
}

static void
greeter_halt_handler (void)
{
	_exit (DISPLAY_HALT);
}

static void
greeter_suspend_handler (void)
{
	printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_SUSPEND);
	fflush (stdout);
}

static void
greeter_config_handler (void)
{
        greeter_item_ulist_disable ();

	/* Make sure to unselect the user */
	greeter_item_ulist_unset_selected_user ();

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	/* configure interruption */
	printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CONFIGURE);
	fflush (stdout);
}

static void
greeter_chooser_handler (void)
{
	_exit (DISPLAY_RUN_CHOOSER);
}

void
greeter_system_append_system_menu (GtkWidget *menu)
{
	GtkWidget *w, *sep;
	gint i = 0;

	/* should never be allowed by the UI */
	if ( ! gdm_config_get_bool (GDM_KEY_SYSTEM_MENU) ||
	    ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
		return;

	if (gdm_config_get_bool (GDM_KEY_CHOOSER_BUTTON)) {
		w = gtk_menu_item_new_with_mnemonic (_("Remote Login via _XDMCP..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (greeter_chooser_handler),
				  NULL);
	}

	/*
	 * Disable Configuration if using accessibility (AddGtkModules) since
	 * using it with accessibility causes a hang.
	 */
	if (gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) &&
	    !gdm_config_get_bool (GDM_KEY_ADD_GTK_MODULES) &&
	    bin_exists (gdm_config_get_string (GDM_KEY_CONFIGURATOR))) {
		w = gtk_menu_item_new_with_mnemonic (_("Confi_gure Login Manager..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (greeter_config_handler),
				  NULL);
	}

	if (GdmRebootFound || GdmHaltFound || GdmSuspendFound || GdmAnyCustomCmdsFound) {
		sep = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
		gtk_widget_show (sep);
	}

	if (GdmRebootFound) {
		w = gtk_menu_item_new_with_mnemonic (_("_Restart"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_restart_handler),
				  NULL);
	}

	if (GdmAnyCustomCmdsFound) {
		for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
		        if (GdmCustomCmdsFound[i]){
				gint * cmd_index = g_new0(gint, 1);
				gchar * key_string = NULL;
				*cmd_index = i;
				key_string = g_strdup_printf (_("%s%d="), GDM_KEY_CUSTOM_CMD_LABEL_TEMPLATE, i);
				w = gtk_menu_item_new_with_mnemonic (gdm_config_get_string(key_string));
				gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
				gtk_widget_show (GTK_WIDGET (w));
				g_signal_connect (G_OBJECT (w), "activate",
						  G_CALLBACK (query_greeter_custom_cmd_handler),
						  cmd_index);
				g_free (key_string);
			}
		}
	}

	if (GdmHaltFound) {
		w = gtk_menu_item_new_with_mnemonic (_("Shut _Down"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_halt_handler),
				  NULL);
	}

	if (GdmSuspendFound) {
		w = gtk_menu_item_new_with_mnemonic (_("Sus_pend"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_suspend_handler),
				  NULL);
	}
}

static gboolean
radio_button_press_event (GtkWidget *widget,
			  GdkEventButton *event,
			  gpointer data)
{
    if (event->type == GDK_2BUTTON_PRESS) {
        gtk_dialog_response (GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    }
    return FALSE;
}

static void
greeter_system_handler (GreeterItemInfo *info,
			gpointer         user_data)
{
  GtkWidget *w = NULL;
  GtkWidget *hbox = NULL;
  GtkWidget *main_vbox = NULL;
  GtkWidget *vbox = NULL;
  GtkWidget *cat_vbox = NULL;
  GtkWidget *group_radio = NULL;
  GtkWidget *halt_radio = NULL;
  GtkWidget *suspend_radio = NULL;
  GtkWidget *restart_radio = NULL;
  GtkWidget **custom_cmds_radio = NULL;
  GtkWidget *config_radio = NULL;
  GtkWidget *chooser_radio = NULL;
  gchar *s;
  int ret;
  gint i;
  GSList *radio_group = NULL;
  static GtkTooltips *tooltips = NULL;

  /* should never be allowed by the UI */
  if ( ! gdm_config_get_bool (GDM_KEY_SYSTEM_MENU) ||
       ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
	  return;

  dialog = gtk_dialog_new ();
  if (tooltips == NULL)
	  tooltips = gtk_tooltips_new ();
  gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
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

  s = g_strdup_printf ("<b>%s</b>",
		       _("Choose an Action"));
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
  gtk_box_pack_start (GTK_BOX (hbox),
		      vbox,
		      TRUE, TRUE, 0);

  if (GdmHaltFound) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  halt_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							   _("Shut _down the computer"));
	  group_radio = halt_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (halt_radio),
				_("Shut Down your computer so that "
				  "you may turn it off."),
				NULL);
	  g_signal_connect (G_OBJECT(halt_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      halt_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (halt_radio);
  }

  if (GdmRebootFound) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  restart_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("_Restart the computer"));
	  group_radio = restart_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (restart_radio),
				_("Restart your computer"),
				NULL);
	  g_signal_connect (G_OBJECT(restart_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      restart_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (restart_radio);
  }

  if (GdmAnyCustomCmdsFound) {
	  custom_cmds_radio = g_new0 (GtkWidget*, GDM_CUSTOM_COMMAND_MAX); 
	  for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
	          custom_cmds_radio[i] = NULL;
		  if (GdmCustomCmdsFound[i]){
		          gchar * key_string = NULL;
			  key_string = g_strdup_printf (_("%s%d="), GDM_KEY_CUSTOM_CMD_LR_LABEL_TEMPLATE, i);
			  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
			  custom_cmds_radio[i] = gtk_radio_button_new_with_mnemonic (radio_group,
										     gdm_config_get_string(key_string));
			  group_radio = custom_cmds_radio[i];
			  g_free (key_string);
			  key_string = g_strdup_printf (_("%s%d="), GDM_KEY_CUSTOM_CMD_TOOLTIP_TEMPLATE, i);
			  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (custom_cmds_radio[i]),
						gdm_config_get_string(key_string),
						NULL);
			  g_signal_connect (G_OBJECT(custom_cmds_radio[i]), "button_press_event",
					    G_CALLBACK(radio_button_press_event), NULL);
			  gtk_box_pack_start (GTK_BOX (vbox),
					      custom_cmds_radio[i],
					      FALSE, FALSE, 4);
			  gtk_widget_show (custom_cmds_radio[i]);
			  g_free (key_string);
		  }
	  }
  }

  if (GdmSuspendFound) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  suspend_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							      _("Sus_pend the computer"));
	  group_radio = suspend_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (suspend_radio),
				_("Suspend your computer"),
				NULL);
	  g_signal_connect (G_OBJECT(suspend_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      suspend_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (suspend_radio);
  }

  if (gdm_config_get_bool (GDM_KEY_CHOOSER_BUTTON)) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  chooser_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("Run _XDMCP chooser"));
	  group_radio = chooser_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (chooser_radio),
				_("Run an XDMCP chooser which will allow "
				  "you to log into available remote "
				  "computers, if there are any."),
				NULL);
	  g_signal_connect (G_OBJECT(chooser_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      chooser_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (chooser_radio);
  }

  /*
   * Disable Configuration if using accessibility (AddGtkModules) since
   * using it with accessibility causes a hang.
   */
  if (gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) &&
      !gdm_config_get_bool (GDM_KEY_ADD_GTK_MODULES) &&
      bin_exists (gdm_config_get_string (GDM_KEY_CONFIGURATOR))) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  config_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("Confi_gure the login manager"));
	  group_radio = config_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (config_radio),
				_("Configure GDM (this login manager). "
				  "This will require the root password."),
				NULL);
	  g_signal_connect (G_OBJECT(config_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      config_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (config_radio);
  }
  
  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_OK,
			 GTK_RESPONSE_OK);

  gtk_dialog_set_default_response (GTK_DIALOG (dialog),
				   GTK_RESPONSE_OK);
  
  gtk_widget_show_all (dialog);
  gdm_wm_center_window (GTK_WINDOW (dialog));

  gdm_wm_no_login_focus_push ();
  ret = gtk_dialog_run (GTK_DIALOG (dialog));
  gdm_wm_no_login_focus_pop ();
  
  if (ret != GTK_RESPONSE_OK)
    {
      gtk_widget_destroy (dialog);
      return;
    }

  if (halt_radio != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (halt_radio)))
    greeter_halt_handler ();
  else if (restart_radio != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (restart_radio)))
    greeter_restart_handler ();
  else if (suspend_radio != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (suspend_radio)))
    greeter_suspend_handler ();
  else if (config_radio != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (config_radio)))
    greeter_config_handler ();
  else if (chooser_radio != NULL && gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (chooser_radio)))
    greeter_chooser_handler ();
  else
    for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
	if (custom_cmds_radio[i] != NULL &&  gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (custom_cmds_radio[i])))
	    greeter_custom_cmd_handler (i);
    }  

  gtk_widget_destroy (dialog);
}


void
greeter_item_system_setup (void)
{  
  gint i;
	
  greeter_item_register_action_callback ("reboot_button",
					 (ActionFunc)query_greeter_restart_handler,
					 NULL);  
  greeter_item_register_action_callback ("halt_button",
					 (ActionFunc)query_greeter_halt_handler,
					 NULL);
  greeter_item_register_action_callback ("suspend_button",
					 (ActionFunc)query_greeter_suspend_handler,
					 NULL);
  greeter_item_register_action_callback ("system_button",
					 (ActionFunc)greeter_system_handler,
					 NULL);
  greeter_item_register_action_callback ("config_button",
					 (ActionFunc)greeter_config_handler,
					 NULL);
  greeter_item_register_action_callback ("chooser_button",
					 (ActionFunc)greeter_chooser_handler,
					 NULL);

  for (i = 0; i < GDM_CUSTOM_COMMAND_MAX; i++) {
	  gint * cmd_index = g_new0(gint, 1);
	  gchar * key_string;
	  *cmd_index = i;
	  key_string = g_strdup_printf (_("custom_cmd_button%d"), i);
	  greeter_item_register_action_callback (key_string,
						 (ActionFunc)query_greeter_custom_cmd_handler,
						 cmd_index);
	  g_free (key_string);
  }
}
