#include "config.h"

#include <gtk/gtk.h>
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_system.h"
#include "greeter_item.h"

#include <syslog.h>
#include <unistd.h>

#include "gdm.h"
#include "gdmwm.h"

static void
greeter_reboot_handler (void)
{
	if (greeter_query (_("Are you sure you want to reboot the machine?"))) {
		closelog();
		
		_exit (DISPLAY_REBOOT);
	}
}


static void
greeter_halt_handler (void)
{
	if (greeter_query (_("Are you sure you want to shut down the machine?"))) {
		closelog();

		_exit (DISPLAY_HALT);
	}
}

static void
greeter_suspend_handler (void)
{
	if (greeter_query (_("Are you sure you want to suspend the machine?"))) {
		closelog();

		_exit (DISPLAY_SUSPEND);
	}
}

static void
greeter_system_handler (GreeterItemInfo *info,
			gpointer         user_data)
{
  GtkWidget *dialog;
  GtkWidget *halt_radio;
  GtkWidget *suspend_radio;
  GtkWidget *reboot_radio;
  int ret;
  GSList *radio_group = NULL;

  dialog = gtk_dialog_new ();

  halt_radio = gtk_radio_button_new_with_mnemonic (NULL,
						   _("Shut down the computer"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      halt_radio,
		      FALSE, FALSE, 4);
  gtk_widget_show (halt_radio);

  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (halt_radio));
  suspend_radio = gtk_radio_button_new_with_mnemonic (radio_group,
						      _("Suspend the computer"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      suspend_radio,
		      FALSE, FALSE, 4);
  gtk_widget_show (suspend_radio);
  
  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (halt_radio));
  reboot_radio = gtk_radio_button_new_with_mnemonic (radio_group,
						     _("Reboot the computer"));
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
		      reboot_radio,
		      FALSE, FALSE, 4);
  gtk_widget_show (reboot_radio);
  
  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_CANCEL,
			 GTK_RESPONSE_CANCEL);

  gtk_dialog_add_button (GTK_DIALOG (dialog),
			 GTK_STOCK_OK,
			 GTK_RESPONSE_OK);
  
  gtk_widget_show_all (dialog);
  gdm_wm_center_window (GTK_WINDOW (dialog));

  gdm_wm_no_login_focus_push ();
  ret = gtk_dialog_run (GTK_DIALOG (dialog));
  gdm_wm_no_login_focus_pop ();
  
  if (ret == GTK_RESPONSE_CANCEL)
    {
      gtk_widget_destroy (dialog);
      return;
    }

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (halt_radio)))
    greeter_halt_handler();
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (reboot_radio)))
    greeter_reboot_handler ();
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (suspend_radio)))
    greeter_suspend_handler ();

  gtk_widget_destroy (dialog);
}


void
greeter_item_system_setup (void)
{
  greeter_item_register_action_callback ("reboot_button",
					 (ActionFunc)greeter_reboot_handler,
					 NULL);
  greeter_item_register_action_callback ("halt_button",
					 (ActionFunc)greeter_halt_handler,
					 NULL);
  greeter_item_register_action_callback ("suspend_button",
					 (ActionFunc)greeter_suspend_handler,
					 NULL);
  greeter_item_register_action_callback ("system_button",
					 (ActionFunc)greeter_system_handler,
					 NULL);
}
