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
#include "gdm-smartcard-extension.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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
        GSettings *settings;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        GPid       worker_pid;
        int        number_of_tokens;

        GQueue    *message_queue;
        guint      message_timeout_id;

        guint      answer_pending : 1;
        guint      select_when_ready : 1;
};

typedef struct {
        char                       *text;
        GdmServiceMessageType  type;
} QueuedMessage;

static void gdm_smartcard_extension_finalize (GObject *object);

static void gdm_login_extension_iface_init (GdmLoginExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmSmartcardExtension,
                         gdm_smartcard_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_LOGIN_EXTENSION,
                                                gdm_login_extension_iface_init));

static void
set_message (GdmSmartcardExtension *extension,
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
purge_message_queue (GdmSmartcardExtension *extension)
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
dequeue_message (GdmSmartcardExtension *extension)
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
                        gdk_window_beep (gtk_widget_get_window (GTK_WIDGET (extension)));
                }

                free_queued_message (message);
        } else {
                extension->priv->message_timeout_id = 0;

                _gdm_login_extension_emit_message_queue_empty (GDM_LOGIN_EXTENSION (extension));
        }

        return FALSE;
}

static void
gdm_smartcard_extension_queue_message (GdmLoginExtension   *login_extension,
                                       GdmServiceMessageType  type,
                                       const char            *text)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);

        QueuedMessage *message = g_slice_new (QueuedMessage);

        message->text = g_strdup (text);
        message->type = type;

        g_queue_push_tail (extension->priv->message_queue, message);

        if (extension->priv->message_timeout_id == 0) {
                dequeue_message (extension);
        }
}

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
                        if (!_gdm_login_extension_emit_choose_user (GDM_LOGIN_EXTENSION (extension),
                                                                    GDM_SMARTCARD_EXTENSION_SERVICE_NAME)) {
                                g_debug ("could not choose smart card user, cancelling...");
                                _gdm_login_extension_emit_cancel (GDM_LOGIN_EXTENSION (extension));
                                extension->priv->select_when_ready = TRUE;
                        } else {
                                g_debug ("chose smart card user!");
                        }
                } else if (extension->priv->number_of_tokens == 0) {
                        _gdm_login_extension_emit_cancel (GDM_LOGIN_EXTENSION (extension));
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
stop_watching_for_smartcards (GdmSmartcardExtension *extension)
{
        kill (extension->priv->worker_pid, SIGTERM);
}

static void
gdm_smartcard_extension_ask_question (GdmLoginExtension *login_extension,
                                      const char          *message)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_action_set_visible (extension->priv->login_action, TRUE);
        gtk_action_set_sensitive (extension->priv->login_action, TRUE);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_smartcard_extension_ask_secret (GdmLoginExtension *login_extension,
                                    const char          *message)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        gtk_action_set_visible (extension->priv->login_action, TRUE);
        gtk_action_set_sensitive (extension->priv->login_action, TRUE);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_smartcard_extension_reset (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        gtk_widget_hide (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_action_set_visible (extension->priv->login_action, FALSE);
        extension->priv->answer_pending = FALSE;

        purge_message_queue (extension);
        set_message (extension, "");

        gdm_login_extension_set_enabled (login_extension, FALSE);
}

static void
gdm_smartcard_extension_set_ready (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        gdm_login_extension_set_enabled (login_extension, TRUE);

        if (extension->priv->worker_pid <= 0) {
                watch_for_smartcards (extension);
        }

        if (extension->priv->select_when_ready) {
                if (_gdm_login_extension_emit_choose_user (login_extension,
                                                           GDM_SMARTCARD_EXTENSION_SERVICE_NAME)) {
                        extension->priv->select_when_ready = FALSE;
                }
        }
}

static char *
gdm_smartcard_extension_get_service_name (GdmLoginExtension *login_extension)
{
        return g_strdup (GDM_SMARTCARD_EXTENSION_SERVICE_NAME);
}

static GtkWidget *
gdm_smartcard_extension_get_page (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        return extension->priv->page;
}

static GtkActionGroup *
gdm_smartcard_extension_get_actions (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);

        return g_object_ref (extension->priv->actions);
}

static void
request_answer (GdmSmartcardExtension *extension)
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
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_action_set_visible (extension->priv->login_action, FALSE);
}

static gboolean
gdm_smartcard_extension_focus (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);

        if (!extension->priv->answer_pending) {
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

static gboolean
gdm_smartcard_extension_has_queued_messages (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);

        if (extension->priv->message_timeout_id != 0) {
                return TRUE;
        }

        if (!g_queue_is_empty (extension->priv->message_queue)) {
                return TRUE;
        }

        return FALSE;
}


static GIcon *
gdm_smartcard_extension_get_icon (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);
        return g_object_ref (extension->priv->icon);
}

static char *
gdm_smartcard_extension_get_name (GdmLoginExtension *login_extension)
{
        return g_strdup (_("Smartcard Authentication"));
}

static char *
gdm_smartcard_extension_get_description (GdmLoginExtension *login_extension)
{
        return g_strdup (_("Log into session with smartcard"));
}

static gboolean
gdm_smartcard_extension_is_choosable (GdmLoginExtension *login_extension)
{
        return TRUE;
}

static gboolean
gdm_smartcard_extension_is_visible (GdmLoginExtension *login_extension)
{
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (login_extension);

        char *contents, **lines, *pid_dir;
        gboolean ret;
        guint i;
        pid_t pid;

        if (!g_settings_get_boolean (extension->priv->settings, "enable-smartcard-authentication")) {
                return FALSE;
        }

        /* FIXME: we should rework things so we find out from the worker that
         * there's no daemon running instead of like this.
         */
        if (g_file_get_contents ("/var/run/pcscd.pid",
                                 &contents, NULL, NULL) == FALSE) {
                return FALSE;
        }

        pid = (pid_t) atoi (contents);
        g_free (contents);

        if (pid == 0) {
                return FALSE;
        }

        pid_dir = g_strdup_printf ("/proc/%d", (int) pid);
        if (!g_file_test (pid_dir, G_FILE_TEST_EXISTS)) {
                g_free (pid_dir);
                return FALSE;
        }
        g_free (pid_dir);

        return TRUE;
}

static void
gdm_login_extension_iface_init (GdmLoginExtensionIface *iface)
{
        iface->get_icon = gdm_smartcard_extension_get_icon;
        iface->get_description = gdm_smartcard_extension_get_description;
        iface->get_name = gdm_smartcard_extension_get_name;
        iface->is_choosable = gdm_smartcard_extension_is_choosable;
        iface->is_visible = gdm_smartcard_extension_is_visible;
        iface->queue_message = gdm_smartcard_extension_queue_message;
        iface->ask_question = gdm_smartcard_extension_ask_question;
        iface->ask_secret = gdm_smartcard_extension_ask_secret;
        iface->reset = gdm_smartcard_extension_reset;
        iface->set_ready = gdm_smartcard_extension_set_ready;
        iface->get_service_name = gdm_smartcard_extension_get_service_name;
        iface->get_page = gdm_smartcard_extension_get_page;
        iface->get_actions = gdm_smartcard_extension_get_actions;
        iface->focus = gdm_smartcard_extension_focus;
        iface->has_queued_messages = gdm_smartcard_extension_has_queued_messages;
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
        GdmSmartcardExtension *extension = GDM_SMARTCARD_EXTENSION (object);

        if (extension->priv->worker_pid > 0) {
                stop_watching_for_smartcards (extension);
        }

        purge_message_queue (extension);
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
        request_answer (extension);
        gtk_action_set_sensitive (extension->priv->login_action, FALSE);
}

static void
create_actions (GdmSmartcardExtension *extension)
{
        GtkAction *action;

        extension->priv->actions = gtk_action_group_new (GDM_SMARTCARD_EXTENSION_NAME);

        action = gtk_action_new (GDM_LOGIN_EXTENSION_DEFAULT_ACTION,
                                 _("Log In"), NULL, NULL);
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

        extension->priv->message_queue = g_queue_new ();

        extension->priv->settings = g_settings_new ("org.gnome.login-screen");

        gdm_smartcard_extension_reset (GDM_LOGIN_EXTENSION (extension));
}

void
g_io_module_load (GIOModule *module)
{
        g_io_extension_point_implement (GDM_LOGIN_EXTENSION_POINT_NAME,
                                        GDM_TYPE_SMARTCARD_EXTENSION,
                                        GDM_SMARTCARD_EXTENSION_NAME,
                                        0);
}

void
g_io_module_unload (GIOModule *module)
{
}
