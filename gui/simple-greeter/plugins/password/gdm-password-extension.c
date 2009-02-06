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
#include "gdm-password-extension.h"
#include "gdm-conversation.h"
#include "gdm-task.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

struct _GdmPasswordExtensionPrivate
{
        GIcon     *icon;
        GtkWidget *page;
        GtkActionGroup *actions;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        guint      answer_pending : 1;
};

static void gdm_password_extension_finalize (GObject *object);

static void gdm_task_iface_init (GdmTaskIface *iface);
static void gdm_conversation_iface_init (GdmConversationIface *iface);
static void gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmPasswordExtension,
                         gdm_password_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_GREETER_EXTENSION,
                                                gdm_greeter_extension_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_TASK,
                                                gdm_task_iface_init)
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_CONVERSATION,
                                                gdm_conversation_iface_init));

static void
gdm_password_extension_set_message (GdmConversation *conversation,
                                    const char *message)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->message_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->message_label), message);
}

static void
gdm_password_extension_ask_question (GdmConversation *conversation,
                                     const char      *message)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_password_extension_ask_secret (GdmConversation *conversation,
                                   const char      *message)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_password_extension_reset (GdmConversation *conversation)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        extension->priv->answer_pending = FALSE;

        gdm_task_set_enabled (GDM_TASK (conversation), FALSE);
}

static void
gdm_password_extension_set_ready (GdmConversation *conversation)
{
        gdm_task_set_enabled (GDM_TASK (conversation), TRUE);
}

char *
gdm_password_extension_get_service_name (GdmConversation *conversation)
{
        return g_strdup (PAMSERVICENAME);
}

GtkWidget *
gdm_password_extension_get_page (GdmConversation *conversation)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        return extension->priv->page;
}

GtkActionGroup *
gdm_password_extension_get_actions (GdmConversation *conversation)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        return g_object_ref (extension->priv->actions);
}

void
gdm_password_extension_request_answer (GdmConversation *conversation)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        const char *text;

        if (!extension->priv->answer_pending) {
                gdm_conversation_answer (conversation, NULL);
                return;
        }

        extension->priv->answer_pending = FALSE;
        text = gtk_entry_get_text (GTK_ENTRY (extension->priv->prompt_entry));
        gdm_conversation_answer (conversation, text);

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
}

gboolean
gdm_password_extension_focus (GdmConversation *conversation)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (conversation);
        if (!extension->priv->answer_pending) {
                gdm_conversation_answer (conversation, NULL);
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

GIcon *
gdm_password_extension_get_icon (GdmTask *task)
{
        GdmPasswordExtension *extension = GDM_PASSWORD_EXTENSION (task);
        return g_object_ref (extension->priv->icon);
}

char *
gdm_password_extension_get_name (GdmTask *task)
{
        return g_strdup (_("Password Authentication"));
}

char *
gdm_password_extension_get_description (GdmTask *task)
{
        return g_strdup (_("Log into session with username and password"));
}

static void
gdm_task_iface_init (GdmTaskIface *iface)
{
        iface->get_icon = gdm_password_extension_get_icon;
        iface->get_description = gdm_password_extension_get_description;
        iface->get_name = gdm_password_extension_get_name;
}

static void
gdm_conversation_iface_init (GdmConversationIface *iface)
{
        iface->set_message = gdm_password_extension_set_message;
        iface->ask_question = gdm_password_extension_ask_question;
        iface->ask_secret = gdm_password_extension_ask_secret;
        iface->reset = gdm_password_extension_reset;
        iface->set_ready = gdm_password_extension_set_ready;
        iface->get_service_name = gdm_password_extension_get_service_name;
        iface->get_page = gdm_password_extension_get_page;
        iface->get_actions = gdm_password_extension_get_actions;
        iface->request_answer = gdm_password_extension_request_answer;
        iface->focus = gdm_password_extension_focus;
}

static void
gdm_greeter_extension_iface_init (GdmGreeterExtensionIface *iface)
{
}

static void
gdm_password_extension_class_init (GdmPasswordExtensionClass *extension_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (extension_class);

        object_class->finalize = gdm_password_extension_finalize;

        g_type_class_add_private (extension_class,
                                  sizeof (GdmPasswordExtensionPrivate));
}

static void
gdm_password_extension_finalize (GObject *object)
{
}

static void
on_activate_log_in (GdmPasswordExtension *extension)
{
        gdm_password_extension_request_answer (GDM_CONVERSATION (extension));
}

static void
create_page (GdmPasswordExtension *extension)
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
create_actions (GdmPasswordExtension *extension)
{
        GtkAction *action;

        extension->priv->actions = gtk_action_group_new ("gdm-password-extension");

        action = gtk_action_new (GDM_CONVERSATION_DEFAULT_ACTION,
                                 _("Log In"),
                                 _("Log into the currently selected sesson"),
                                 NULL);
        g_signal_connect_swapped (action, "activate",
                                  G_CALLBACK (on_activate_log_in), extension);
        g_object_set (G_OBJECT (action), "icon-name", "go-home", NULL);
        gtk_action_group_add_action (extension->priv->actions,
                                     action);
}

static void
gdm_password_extension_init (GdmPasswordExtension *extension)
{
        extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension,
                                                       GDM_TYPE_PASSWORD_EXTENSION,
                                                       GdmPasswordExtensionPrivate);

        extension->priv->icon = g_themed_icon_new ("dialog-password");
        create_page (extension);
        create_actions (extension);

        gdm_password_extension_reset (GDM_CONVERSATION (extension));
}
