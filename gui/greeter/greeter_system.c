#include "config.h"

#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_system.h"
#include "greeter_item.h"

#include <syslog.h>
#include <unistd.h>

#include "gdm.h"
#include "gdmwm.h"
#include "vicious.h"

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

static gboolean
working_command_exists (const char *commands)
{
	char *command = ve_get_first_working_command
		(commands, TRUE /* only_existance */);
	if (command == NULL)
		return FALSE;
	g_free (command);
	return TRUE;
}

static void
query_greeter_reboot_handler (void)
{
	if (greeter_query (_("Are you sure you want to reboot the machine?"))) {
		closelog();
		
		_exit (DISPLAY_REBOOT);
	}
}


static void
query_greeter_halt_handler (void)
{
	if (greeter_query (_("Are you sure you want to shut down the machine?"))) {
		closelog();

		_exit (DISPLAY_HALT);
	}
}

static void
query_greeter_suspend_handler (void)
{
	if (greeter_query (_("Are you sure you want to suspend the machine?"))) {
		closelog();

		_exit (DISPLAY_SUSPEND);
	}
}

static void
greeter_reboot_handler (void)
{
	/*if (greeter_query (_("Are you sure you want to reboot the machine?"))) {*/
		closelog();
		
		_exit (DISPLAY_REBOOT);
	/*}*/
}


static void
greeter_halt_handler (void)
{
	/* if (greeter_query (_("Are you sure you want to shut down the machine?"))) { */
		closelog();

		_exit (DISPLAY_HALT);
	/* } */
}

static void
greeter_suspend_handler (void)
{
	/* if (greeter_query (_("Are you sure you want to suspend the machine?"))) { */
		closelog();

		_exit (DISPLAY_SUSPEND);
	/* } */
}

static void
greeter_config_handler (void)
{
	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

#if 0
	/* Taken from gdmlogin, does this apply? */
	/* configure interruption */
	login_entry = FALSE; /* no matter where we are,
				this is no longer a login_entry */
#endif
	/* configure interruption */
	printf ("%c%c%c\n", STX, BEL, GDM_INTERRUPT_CONFIGURE);
	fflush (stdout);
}


static void
greeter_system_handler (GreeterItemInfo *info,
			gpointer         user_data)
{
  GtkWidget *dialog;
  GtkWidget *group_radio = NULL;
  GtkWidget *halt_radio = NULL;
  GtkWidget *suspend_radio = NULL;
  GtkWidget *reboot_radio = NULL;
  GtkWidget *config_radio = NULL;
  int ret;
  GSList *radio_group = NULL;

  /* should never be allowed by the UI */
  if ( ! GdmSystemMenu)
	  return;

  dialog = gtk_dialog_new ();

  if (working_command_exists (GdmHalt)) {
	  halt_radio = gtk_radio_button_new_with_mnemonic (NULL,
							   _("Shut down the computer"));
	  group_radio = halt_radio;
	  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			      halt_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (halt_radio);
  }

  if (working_command_exists (GdmSuspend)) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  suspend_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							      _("Suspend the computer"));
	  group_radio = suspend_radio;
	  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			      suspend_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (suspend_radio);
  }
  
  if (working_command_exists (GdmReboot)) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  reboot_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("Reboot the computer"));
	  group_radio = reboot_radio;
	  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			      reboot_radio,
			      FALSE, FALSE, 4);
	  gtk_widget_show (reboot_radio);
  }

  if (GdmConfigAvailable &&
      bin_exists (GdmConfigurator)) {
	  if (group_radio != NULL)
		  radio_group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (group_radio));
	  config_radio = gtk_radio_button_new_with_mnemonic (radio_group,
							     _("Configure"));
	  group_radio = config_radio;
	  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
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

  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (halt_radio)))
    greeter_halt_handler();
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (reboot_radio)))
    greeter_reboot_handler ();
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (suspend_radio)))
    greeter_suspend_handler ();
  else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (config_radio)))
    greeter_config_handler ();

  gtk_widget_destroy (dialog);
}


void
greeter_item_system_setup (void)
{
  greeter_item_register_action_callback ("reboot_button",
					 (ActionFunc)query_greeter_reboot_handler,
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
}
