/*
 * Copyright (C) 2009 Red Hat, Inc.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Written By: Ray Strode <rstrode@redhat.com>
 *
 */

#include <config.h>
#include "gdm-smartcard-extension.h"
#include "gdm-conversation.h"
#include "gdm-task.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#ifndef GDM_SMARTCARD_WORKER_COMMAND
#define GDM_SMARTCARD_WORKER_COMMAND LIBEXECDIR "/gdm-smartcard-worker"
#endif

struct _GdmSmartcardExtensionPrivate
{
        GIcon     *icon;
        GtkWidget *page;
        GtkActionGroup *actions;
        GtkAction  *login_action;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        GPid       worker_pid;
        int        number_of_tokens;

        guint      answer_pending : 1;
};

static void gdm_smartcard_extension_finalize (GObject *object);

static void gdm_task_iface_init (GdmTaskIface *iface);
static void gdm_conversation_iface_init (GdmConversationIface *iface);
static void gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmSmartcardExtension,
                         gdm_smartcard_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_GREETER_EXTENSION,
                                                gdm_greeter_extension_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_TASK,
                                                gdm_task_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_CONVERSATION,
                                                gdm_conversation_iface_init));

static gboolean
on_smartcard_event (GIOChannel   *io_channel,
                    GIOCondition  condition,
                    gpointer      data)
{
        GdmSmartcardExtension *extension;

        extension = GDM_SMARTCARD_EXTENSION (data);

        if (condition & G_IO_IN) {
                char buffer[1024];
                ssize_t num_bytes;

                num_bytes = read (g_io_channel_unix_get_fd (io_channel),
                                  buffer, sizeof (buffer));

                if (num_bytes < 0 && errno != EINTR)
                        return FALSE;

                if (num_bytes != 1) {
                        g_debug ("buffer: %s\n", buffer);
                        return TRUE;
                }

                if (buffer[0] == 'I') {
                        extension->priv->number_of_tokens++;
                } else {
                        extension->priv->number_of_tokens--;
                }

                if (extension->priv->number_of_tokens == 1) {
                        gdm_conversation_choose_user (GDM_CONVERSATION (extension),
                                                      PAMSERVICENAME);
                } else if (extension->priv->number_of_tokens == 0) {
                        gdm_conversation_cancel (GDM_CONVERSATION (extension));
                }

                return TRUE;
        }

        if (condition & G_IO_HUP) {
                return FALSE;
        }

        return TRUE;
}

static void
watch_for_smartcards (GdmSmartcardExtension *extension)
{
        GError *error;
        GIOChannel *io_channel;
        char *args[] = { GDM_SMARTCARD_WORKER_COMMAND, NULL };
        GPid pid;
        int stdout_fd;

        error = NULL;

        if (!g_spawn_async_with_pipes (NULL, args, NULL, 0,
                                       NULL, NULL, &pid, NULL,
                                       &stdout_fd, NULL, &error)) {
                g_debug ("could not start smart card manager: %s", error->message);
                g_error_free (error);
                return;
        }
        fcntl (stdout_fd, F_SETFD, FD_CLOEXEC);

        io_channel = g_io_channel_unix_new (stdout_fd);
        g_io_channel_set_flags (io_channel, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_encoding (io_channel, NULL, NULL);
        g_io_channel_set_buffered (io_channel, FALSE);
        g_io_add_watch (io_channel, G_IO_IN, on_smartcard_event, extension);
        g_io_channel_set_close_on_unref (io_channel, TRUE);
        g_io_channel_unref (io_channel);

        extension->priv->worker_pid = pid;
}

static void
gdm_smartcard_extension_set_message (GdmConversation *conversation,
                                       const char *message)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->message_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->message_label), message);
}

static void
gdm_smartcard_extension_ask_question (GdmConversation *conversation,
                                        const char      *message)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_action_set_visible (extension->priv->login_action, TRUE);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_smartcard_extension_ask_secret (GdmConversation *conversation,
                                      const char      *message)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        gtk_action_set_visible (extension->priv->login_action, TRUE);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_smartcard_extension_reset (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_action_set_visible (extension->priv->login_action, FALSE);
        extension->priv->answer_pending = FALSE;

        gdm_task_set_enabled (GDM_TASK (conversation), FALSE);
}

static void
gdm_smartcard_extension_set_ready (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        gdm_task_set_enabled (GDM_TASK (conversation), TRUE);

        if (extension->priv->worker_pid <= 0)
          {
                watch_for_smartcards (extension);
          }
}

char *
gdm_smartcard_extension_get_service_name (GdmConversation *conversation)
{
        return g_strdup (PAMSERVICENAME);
}

GtkWidget *
gdm_smartcard_extension_get_page (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        return extension->priv->page;
}

GtkActionGroup *
gdm_smartcard_extension_get_actions (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);

        return g_object_ref (extension->priv->actions);
}

void
gdm_smartcard_extension_request_answer (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);
        const char *text;

        if (!extension->priv->answer_pending) {
                gdm_conversation_answer (conversation, NULL);
                return;
        }

        extension->priv->answer_pending = FALSE;
        text = gtk_entry_get_text (GTK_ENTRY (extension->priv->prompt_entry));
        gdm_conversation_answer (conversation, text);

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_action_set_visible (extension->priv->login_action, FALSE);
}

gboolean
gdm_smartcard_extension_focus (GdmConversation *conversation)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (conversation);

        if (!extension->priv->answer_pending) {
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

GIcon *
gdm_smartcard_extension_get_icon (GdmTask *task)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (task);
        return g_object_ref (extension->priv->icon);
}

char *
gdm_smartcard_extension_get_name (GdmTask *task)
{
        return g_strdup (_("Smartcard Authentication"));
}

char *
gdm_smartcard_extension_get_description (GdmTask *task)
{
        return g_strdup (_("Log into session with smartcard"));
}

gboolean
gdm_smartcard_extension_is_choosable (GdmTask *task)
{
        return TRUE;
}

static void
gdm_task_iface_init (GdmTaskIface *iface)
{
        iface->get_icon = gdm_smartcard_extension_get_icon;
        iface->get_description = gdm_smartcard_extension_get_description;
        iface->get_name = gdm_smartcard_extension_get_name;
        iface->is_choosable = gdm_smartcard_extension_is_choosable;
}

static void
gdm_conversation_iface_init (GdmConversationIface *iface)
{
        iface->set_message = gdm_smartcard_extension_set_message;
        iface->ask_question = gdm_smartcard_extension_ask_question;
        iface->ask_secret = gdm_smartcard_extension_ask_secret;
        iface->reset = gdm_smartcard_extension_reset;
        iface->set_ready = gdm_smartcard_extension_set_ready;
        iface->get_service_name = gdm_smartcard_extension_get_service_name;
        iface->get_page = gdm_smartcard_extension_get_page;
        iface->get_actions = gdm_smartcard_extension_get_actions;
        iface->request_answer = gdm_smartcard_extension_request_answer;
        iface->focus = gdm_smartcard_extension_focus;
}

static void
gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface)
{
}

static void
gdm_smartcard_extension_class_init (GdmSmartcardExtensionClass *extension_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (extension_class);

        object_class->finalize = gdm_smartcard_extension_finalize;

        g_type_class_add_private (extension_class,
                                  sizeof (GdmSmartcardExtensionPrivate));
}

static void
gdm_smartcard_extension_finalize (GObject *object)
{
}

static void
create_page (GdmSmartcardExtension *extension)
{
        GtkBuilder *builder;
        GObject *object;
        GError *error;

        builder = gtk_builder_new ();

        error = NULL;
        gtk_builder_add_from_file (builder,
                                   PLUGINDATADIR "/page.ui",
                                   &error);

        if (error != NULL) {
                g_warning ("Could not load UI file: %s", error->message);
                g_error_free (error);
                return;
        }

        object = gtk_builder_get_object (builder, "page");
        g_object_ref (object);

        extension->priv->page = GTK_WIDGET (object);

        object = gtk_builder_get_object (builder, "auth-prompt-label");
        g_object_ref (object);
        extension->priv->prompt_label = GTK_WIDGET (object);
        gtk_widget_hide (extension->priv->prompt_label);

        object = gtk_builder_get_object (builder, "auth-prompt-entry");
        g_object_ref (object);
        extension->priv->prompt_entry = GTK_WIDGET (object);
        gtk_widget_hide (extension->priv->prompt_entry);

        object = gtk_builder_get_object (builder, "auth-message-label");
        g_object_ref (object);
        extension->priv->message_label = GTK_WIDGET (object);
        gtk_widget_show (extension->priv->message_label);

        g_object_unref (builder);
}

static void
on_activate_log_in (GdmSmartcardExtension *extension)
{
        gdm_smartcard_extension_request_answer (GDM_CONVERSATION (extension));
}

static void
create_actions (GdmSmartcardExtension *extension)
{
        GtkAction *action;

        extension->priv->actions = gtk_action_group_new ("gdm-smartcard-extension");

        action = gtk_action_new (GDM_CONVERSATION_DEFAULT_ACTION,
                                 _("Log In"),
                                 _("Log into the currently selected sesson"),
                                 NULL);
        g_signal_connect_swapped (action, "activate",
                                  G_CALLBACK (on_activate_log_in), extension);
        g_object_set (G_OBJECT (action), "icon-name", "go-home", NULL);
        gtk_action_group_add_action (extension->priv->actions,
                                     action);

        gtk_action_set_visible (action, FALSE);
        extension->priv->login_action = action;
}

static void
gdm_smartcard_extension_init (GdmSmartcardExtension *extension)
{
        extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension,
                                                       GDM_TYPE_SMARTCARD_EXTENSION,
                                                       GdmSmartcardExtensionPrivate);

        extension->priv->icon = g_themed_icon_new ("gdm-smartcard");
        create_page (extension);
        create_actions (extension);
        gdm_smartcard_extension_reset (GDM_CONVERSATION (extension));
}
