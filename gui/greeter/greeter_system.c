#include "config.h"

#include <gtk/gtk.h>
#include "greeter.h"
#include "greeter_configuration.h"
#include "greeter_system.h"
#include "greeter_item.h"

#include <syslog.h>

#include "gdm.h"
#include "gdmwm.h"

static gboolean
greeter_query (const gchar *msg)
{
	int ret;
	GtkWidget *req;

	/* we should be now fine for focusing new windows */
	gdm_wm_focus_new_windows (TRUE);

	req = gtk_message_dialog_new (NULL /* parent */,
				      GTK_DIALOG_MODAL /* flags */,
				      GTK_MESSAGE_QUESTION,
				      GTK_BUTTONS_YES_NO,
				      "%s",
				      msg);

	g_signal_connect (G_OBJECT (req), "destroy",
			  G_CALLBACK (gtk_widget_destroyed),
			  &req);

	gdm_wm_center_window (GTK_WINDOW (req));

	gdm_wm_no_login_focus_push ();
	ret = gtk_dialog_run (GTK_DIALOG (req));
	gdm_wm_no_login_focus_pop ();

	if (req != NULL)
	  gtk_widget_destroy (req);

	if (ret == GTK_RESPONSE_YES)
		return TRUE;
	else /* this includes window close */
		return FALSE;
}


static void
greeter_reboot_handler (GreeterItemInfo *info,
			gpointer         user_data)
{
	if (greeter_query (_("Are you sure you want to reboot the machine?"))) {
		closelog();
		
		_exit (DISPLAY_REBOOT);
	}
}


static void
greeter_halt_handler (GreeterItemInfo *info,
		      gpointer         user_data)
{
	if (greeter_query (_("Are you sure you want to shut down the machine?"))) {
		closelog();

		_exit (DISPLAY_HALT);
	}
}

static void
greeter_suspend_handler (GreeterItemInfo *info,
			 gpointer         user_data)
{
	if (greeter_query (_("Are you sure you want to suspend the machine?"))) {
		closelog();

		_exit (DISPLAY_SUSPEND);
	}
}

void
greeter_item_system_setup (void)
{
  greeter_item_register_action_callback ("reboot_button",
					 greeter_reboot_handler,
					 NULL);
  greeter_item_register_action_callback ("halt_button",
					 greeter_halt_handler,
					 NULL);
  greeter_item_register_action_callback ("suspend_button",
					 greeter_suspend_handler,
					 NULL);

}
