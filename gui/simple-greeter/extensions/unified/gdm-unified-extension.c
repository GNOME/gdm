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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Written By: Ray Strode <rstrode@redhat.com>
 *
 */

#include <config.h>
#include "gdm-unified-extension.h"
#include "gdm-login-extension.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

struct _GdmUnifiedExtensionPrivate
{
        GIcon     *icon;
        GtkWidget *page;
        GtkActionGroup *actions;
        GtkAction *login_action;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        GQueue    *message_queue;
        guint      message_timeout_id;

        guint      answer_pending : 1;
};

typedef struct {
        char                       *text;
        GdmServiceMessageType  type;
} QueuedMessage;

static void gdm_unified_extension_finalize (GObject *object);

static void gdm_login_extension_iface_init (GdmLoginExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmUnifiedExtension,
                         gdm_unified_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_LOGIN_EXTENSION,
                                                gdm_login_extension_iface_init));

static void
set_message (GdmUnifiedExtension *extension,
             const char           *message)
{
        gtk_widget_show (extension->priv->message_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->message_label), message);
}

static void
free_queued_message (QueuedMessage *message)
{
        g_free (message->text);
        g_slice_free (QueuedMessage, message);
}

static void
purge_message_queue (GdmUnifiedExtension *extension)
{
        if (extension->priv->message_timeout_id) {
                g_source_remove (extension->priv->message_timeout_id);
                extension->priv->message_timeout_id = 0;
        }
        g_queue_foreach (extension->priv->message_queue,
                         (GFunc) free_queued_message,
                         NULL);
        g_queue_clear (extension->priv->message_queue);
}

static gboolean
dequeue_message (GdmUnifiedExtension *extension)
{
        if (!g_queue_is_empty (extension->priv->message_queue)) {
                int duration;
                gboolean needs_beep;

                QueuedMessage *message;
                message = (QueuedMessage *) g_queue_pop_head (extension->priv->message_queue);

                switch (message->type) {
                        case GDM_SERVICE_MESSAGE_TYPE_INFO:
                                needs_beep = FALSE;
                                break;
                        case GDM_SERVICE_MESSAGE_TYPE_PROBLEM:
                                needs_beep = TRUE;
                                break;
                        default:
                                g_assert_not_reached ();
                }

                set_message (extension, message->text);

                duration = (int) (g_utf8_strlen (message->text, -1) / 66.0) * 1000;
                duration = CLAMP (duration, 400, 3000);

                extension->priv->message_timeout_id = g_timeout_add (duration,
                                                                     (GSourceFunc) dequeue_message,
                                                                     extension);
                if (needs_beep) {
                        gdk_window_beep (gtk_widget_get_window (GTK_WIDGET (extension->priv->page)));
                }

                free_queued_message (message);
        } else {
                extension->priv->message_timeout_id = 0;

                _gdm_login_extension_emit_message_queue_empty (GDM_LOGIN_EXTENSION (extension));
        }

        return FALSE;
}

static void
gdm_unified_extension_queue_message (GdmLoginExtension   *login_extension,
                                      GdmServiceMessageType  type,
                                      const char            *text)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);

        QueuedMessage *message = g_slice_new (QueuedMessage);

        message->text = g_strdup (text);
        message->type = type;

        g_queue_push_tail (extension->priv->message_queue, message);

        if (extension->priv->message_timeout_id == 0) {
                dequeue_message (extension);
        }
}

static void
gdm_unified_extension_ask_question (GdmLoginExtension *login_extension,
                                     const char          *message)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;

        gtk_action_set_sensitive (extension->priv->login_action, TRUE);
}

static void
gdm_unified_extension_ask_secret (GdmLoginExtension *login_extension,
                                   const char          *message)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;

        gtk_action_set_sensitive (extension->priv->login_action, TRUE);
}

static void
gdm_unified_extension_reset (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        extension->priv->answer_pending = FALSE;

        set_message (extension, "");
        purge_message_queue (extension);

        gdm_login_extension_set_enabled (login_extension, FALSE);
}

static void
gdm_unified_extension_set_ready (GdmLoginExtension *extension)
{
        gdm_login_extension_set_enabled (extension, TRUE);
}

static char *
gdm_unified_extension_get_service_name (GdmLoginExtension *extension)
{
        return g_strdup (GDM_UNIFIED_EXTENSION_SERVICE_NAME);
}

static GtkWidget *
gdm_unified_extension_get_page (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        return extension->priv->page;
}

static GtkActionGroup *
gdm_unified_extension_get_actions (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        return g_object_ref (extension->priv->actions);
}

static void
request_answer (GdmUnifiedExtension *extension)
{
        const char *text;

        if (!extension->priv->answer_pending) {
                _gdm_login_extension_emit_answer (GDM_LOGIN_EXTENSION (extension), NULL);
                return;
        }

        extension->priv->answer_pending = FALSE;
        text = gtk_entry_get_text (GTK_ENTRY (extension->priv->prompt_entry));
        _gdm_login_extension_emit_answer (GDM_LOGIN_EXTENSION (extension), text);

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
}

static gboolean
gdm_unified_extension_focus (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        if (!extension->priv->answer_pending) {
                _gdm_login_extension_emit_answer (login_extension, NULL);
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

static gboolean
gdm_unified_extension_has_queued_messages (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);

        if (extension->priv->message_timeout_id != 0) {
                return TRUE;
        }

        if (!g_queue_is_empty (extension->priv->message_queue)) {
                return TRUE;
        }

        return FALSE;
}

static GIcon *
gdm_unified_extension_get_icon (GdmLoginExtension *login_extension)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (login_extension);
        return g_object_ref (extension->priv->icon);
}

static char *
gdm_unified_extension_get_name (GdmLoginExtension *login_extension)
{
        return g_strdup (_("Authentication"));
}

static char *
gdm_unified_extension_get_description (GdmLoginExtension *login_extension)
{
        return g_strdup (_("Log into session"));
}

static gboolean
gdm_unified_extension_is_choosable (GdmLoginExtension *login_extension)
{
        return FALSE;
}

static gboolean
gdm_unified_extension_is_visible (GdmLoginExtension *login_extension)
{
        return TRUE;
}

static void
gdm_login_extension_iface_init (GdmLoginExtensionIface *iface)
{
        iface->get_icon = gdm_unified_extension_get_icon;
        iface->get_description = gdm_unified_extension_get_description;
        iface->get_name = gdm_unified_extension_get_name;
        iface->is_choosable = gdm_unified_extension_is_choosable;
        iface->is_visible = gdm_unified_extension_is_visible;
        iface->queue_message = gdm_unified_extension_queue_message;
        iface->ask_question = gdm_unified_extension_ask_question;
        iface->ask_secret = gdm_unified_extension_ask_secret;
        iface->reset = gdm_unified_extension_reset;
        iface->set_ready = gdm_unified_extension_set_ready;
        iface->get_service_name = gdm_unified_extension_get_service_name;
        iface->get_page = gdm_unified_extension_get_page;
        iface->get_actions = gdm_unified_extension_get_actions;
        iface->focus = gdm_unified_extension_focus;
        iface->has_queued_messages = gdm_unified_extension_has_queued_messages;
}

static void
gdm_unified_extension_class_init (GdmUnifiedExtensionClass *extension_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (extension_class);

        object_class->finalize = gdm_unified_extension_finalize;

        g_type_class_add_private (extension_class,
                                  sizeof (GdmUnifiedExtensionPrivate));
}

static void
gdm_unified_extension_finalize (GObject *object)
{
        GdmUnifiedExtension *extension = GDM_UNIFIED_EXTENSION (object);

        purge_message_queue (extension);
}

static void
on_activate_log_in (GdmUnifiedExtension *extension,
                    GtkAction            *action)
{
        request_answer (extension);
        gtk_action_set_sensitive (action, FALSE);
}

static void
create_page (GdmUnifiedExtension *extension)
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
create_actions (GdmUnifiedExtension *extension)
{
        GtkAction *action;

        extension->priv->actions = gtk_action_group_new (GDM_UNIFIED_EXTENSION_NAME);

        action = gtk_action_new (GDM_LOGIN_EXTENSION_DEFAULT_ACTION,
                                 _("Log In"), NULL, NULL);
        g_signal_connect_swapped (action, "activate",
                                  G_CALLBACK (on_activate_log_in), extension);
        g_object_set (G_OBJECT (action), "icon-name", "go-home", NULL);
        gtk_action_group_add_action (extension->priv->actions,
                                     action);

        extension->priv->login_action = action;
}

static void
gdm_unified_extension_init (GdmUnifiedExtension *extension)
{
        extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension,
                                                       GDM_TYPE_UNIFIED_EXTENSION,
                                                       GdmUnifiedExtensionPrivate);

        extension->priv->icon = g_themed_icon_new ("dialog-unified");
        create_page (extension);
        create_actions (extension);

        extension->priv->message_queue = g_queue_new ();

        gdm_unified_extension_reset (GDM_LOGIN_EXTENSION (extension));
}

void
gdm_unified_extension_load (void)
{
        g_io_extension_point_implement (GDM_LOGIN_EXTENSION_POINT_NAME,
                                        GDM_TYPE_UNIFIED_EXTENSION,
                                        GDM_UNIFIED_EXTENSION_NAME,
                                        G_MAXINT);
}
