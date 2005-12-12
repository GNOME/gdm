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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_system.h"
#include "greeter_item.h"

#include <syslog.h>
#include <unistd.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "gdmwm.h"
#include "misc.h"

GtkWidget *dialog;

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
	    access (bin, F_OK) == 0) {
		g_free (bin);
		return TRUE;
	} else {
		g_free (bin);
		return FALSE;
	}
}

static void
query_greeter_restart_handler (void)
{
	if (gdm_common_warn (_("Are you sure you want to restart the computer?"), "",
			     _("_Reboot"), NULL, TRUE) == GTK_RESPONSE_YES) {
		closelog ();
		
		_exit (DISPLAY_REBOOT);
	}
}


static void
query_greeter_halt_handler (void)
{
	if (gdm_common_warn (_("Are you sure you want to shut down the computer?"), "",
			     _("Shut _Down"), NULL, TRUE) == GTK_RESPONSE_YES) {
		closelog ();

		_exit (DISPLAY_HALT);
	}
}

static void
query_greeter_suspend_handler (void)
{
	if (gdm_common_warn (_("Are you sure you want to suspend the computer?"), "",
			     _("_Suspend"), NULL, TRUE) == GTK_RESPONSE_YES) {
		/* suspend interruption */
		printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_SUSPEND);
		fflush (stdout);
	}
}

static void
greeter_restart_handler (void)
{
	closelog ();
	_exit (DISPLAY_REBOOT);
}


static void
greeter_halt_handler (void)
{
	closelog ();
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
	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	/* configure interruption */
	printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CONFIGURE);
	fflush (stdout);
}

static void
greeter_chooser_handler (void)
{
	closelog ();
	_exit (DISPLAY_RUN_CHOOSER);
}

void
greeter_system_append_system_menu (GtkWidget *menu)
{
	GtkWidget *w, *sep;
	static GtkTooltips *tooltips = NULL;
	gboolean reboot_cmd, halt_cmd, suspend_cmd;

	/* should never be allowed by the UI */
	if ( ! gdm_config_get_bool (GDM_KEY_SYSTEM_MENU) ||
	    ve_string_empty (g_getenv ("GDM_IS_LOCAL")))
		return;

	if (tooltips == NULL)
		tooltips = gtk_tooltips_new ();

	if (gdm_config_get_bool (GDM_KEY_CHOOSER_BUTTON)) {
		w = gtk_menu_item_new_with_mnemonic (_("_XDMCP Chooser..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (greeter_chooser_handler),
				  NULL);
		gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
				      _("Run an XDMCP chooser which will allow "
					"you to log into available remote "
					"computers, if there are any."),
				      NULL);
	}

	if (gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) &&
	    bin_exists (gdm_config_get_string (GDM_KEY_CONFIGURATOR))) {
		w = gtk_menu_item_new_with_mnemonic (_("_Configure Login Manager..."));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (greeter_config_handler),
				  NULL);
		gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
				      _("Configure GDM (this login manager). "
					"This will require the root password."),
				      NULL);
	}

	reboot_cmd  = gdm_working_command_exists
	   (gdm_config_get_string (GDM_KEY_REBOOT));
	halt_cmd    = gdm_working_command_exists
	   (gdm_config_get_string (GDM_KEY_HALT));
	suspend_cmd = gdm_working_command_exists
	   (gdm_config_get_string (GDM_KEY_SUSPEND));

	if (reboot_cmd || halt_cmd || suspend_cmd) {
		sep = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), sep);
		gtk_widget_show (sep);
	}

	if (reboot_cmd) {
		w = gtk_menu_item_new_with_mnemonic (_("_Reboot"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_restart_handler),
				  NULL);
		gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
				      _("Reboot your computer"),
				      NULL);
	}

	if (halt_cmd) {
		w = gtk_menu_item_new_with_mnemonic (_("Shut _Down"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_halt_handler),
				  NULL);
		gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
				      _("Shut down the system so that "
					"you may safely turn off the computer."),
				      NULL);
	}

	if (suspend_cmd) {
		w = gtk_menu_item_new_with_mnemonic (_("Sus_pend"));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), w);
		gtk_widget_show (GTK_WIDGET (w));
		g_signal_connect (G_OBJECT (w), "activate",
				  G_CALLBACK (query_greeter_suspend_handler),
				  NULL);
		gtk_tooltips_set_tip (tooltips, GTK_WIDGET (w),
				      _("Suspend your computer"),
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
  GtkWidget *config_radio = NULL;
  GtkWidget *chooser_radio = NULL;
  gboolean reboot_cmd, halt_cmd, suspend_cmd;
  gchar *s;
  int ret;
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

  reboot_cmd  = gdm_working_command_exists
     (gdm_config_get_string (GDM_KEY_REBOOT));
  halt_cmd    = gdm_working_command_exists
     (gdm_config_get_string (GDM_KEY_HALT));
  suspend_cmd = gdm_working_command_exists
     (gdm_config_get_string (GDM_KEY_SUSPEND));

  if (halt_cmd) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  halt_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							   _("Shut _down the computer"));
	  group_radio = halt_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (halt_radio),
				_("Shut down your computer so that "
				  "you may turn it off."),
				NULL);
	  g_signal_connect (G_OBJECT(halt_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      halt_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (halt_radio);
  }

  if (reboot_cmd) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  restart_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("_Reboot the computer"));
	  group_radio = restart_radio;
	  gtk_tooltips_set_tip (tooltips, GTK_WIDGET (restart_radio),
				_("Reboot your computer"),
				NULL);
	  g_signal_connect (G_OBJECT(restart_radio), "button_press_event",
			    G_CALLBACK(radio_button_press_event), NULL);
	  gtk_box_pack_start (GTK_BOX (vbox),
			      restart_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (restart_radio);
  }

  if (suspend_cmd) {
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

  if (gdm_config_get_bool (GDM_KEY_CONFIG_AVAILABLE) &&
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

  gtk_widget_destroy (dialog);
}


void
greeter_item_system_setup (void)
{
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
}
