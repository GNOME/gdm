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
#include <stdlib.h>

#include "gdm-fingerprint-extension.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

struct _GdmFingerprintExtensionPrivate
{
        GIcon     *icon;
        GtkWidget *page;
        GtkActionGroup *actions;
        GSettings *settings;

        GtkWidget *message_label;
        GtkWidget *prompt_label;
        GtkWidget *prompt_entry;

        GQueue    *message_queue;
        guint      message_timeout_id;

        GDBusConnection *bus_connection;

        guint      answer_pending : 1;
};

typedef struct {
        char                  *text;
        GdmServiceMessageType  type;
} QueuedMessage;

static void gdm_fingerprint_extension_finalize (GObject *object);

static void gdm_login_extension_iface_init (GdmLoginExtensionIface *iface);

G_DEFINE_TYPE_WITH_CODE (GdmFingerprintExtension,
                         gdm_fingerprint_extension,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDM_TYPE_LOGIN_EXTENSION,
                                                gdm_login_extension_iface_init));

static void
set_message (GdmFingerprintExtension *extension,
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
purge_message_queue (GdmFingerprintExtension *extension)
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
dequeue_message (GdmFingerprintExtension *extension)
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
gdm_fingerprint_extension_queue_message (GdmLoginExtension *login_extension,
                                         GdmServiceMessageType type,
                                         const char      *text)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);

        QueuedMessage *message = g_slice_new (QueuedMessage);

        message->text = g_strdup (text);
        message->type = type;

        g_queue_push_tail (extension->priv->message_queue, message);

        if (extension->priv->message_timeout_id == 0) {
                dequeue_message (extension);
        }
}

static void
gdm_fingerprint_extension_ask_question (GdmLoginExtension *login_extension,
                                        const char      *message)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), TRUE);
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_fingerprint_extension_ask_secret (GdmLoginExtension *login_extension,
                                      const char      *message)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        gtk_widget_show (extension->priv->prompt_label);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), message);
        gtk_entry_set_visibility (GTK_ENTRY (extension->priv->prompt_entry), FALSE);
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
        gtk_widget_show (extension->priv->prompt_entry);
        gtk_widget_grab_focus (extension->priv->prompt_entry);
        extension->priv->answer_pending = TRUE;
}

static void
gdm_fingerprint_extension_reset (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
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
gdm_fingerprint_extension_set_ready (GdmLoginExtension *login_extension)
{
        gdm_login_extension_set_enabled (login_extension, TRUE);
}

static char *
gdm_fingerprint_extension_get_service_name (GdmLoginExtension *login_extension)
{
        return g_strdup (GDM_FINGERPRINT_EXTENSION_SERVICE_NAME);
}

static GtkWidget *
gdm_fingerprint_extension_get_page (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        return extension->priv->page;
}

static GtkActionGroup *
gdm_fingerprint_extension_get_actions (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);

        return g_object_ref (extension->priv->actions);
}

static void
gdm_fingerprint_extension_request_answer (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        const char *text;

        if (!extension->priv->answer_pending) {
                _gdm_login_extension_emit_answer (login_extension, NULL);
                return;
        }

        extension->priv->answer_pending = FALSE;
        text = gtk_entry_get_text (GTK_ENTRY (extension->priv->prompt_entry));
        _gdm_login_extension_emit_answer (login_extension, text);

        gtk_widget_hide (extension->priv->prompt_entry);
        gtk_label_set_text (GTK_LABEL (extension->priv->prompt_label), "");
        gtk_entry_set_text (GTK_ENTRY (extension->priv->prompt_entry), "");
}

static gboolean
gdm_fingerprint_extension_focus (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);

        if (!extension->priv->answer_pending) {
                return FALSE;
        }

        gtk_widget_grab_focus (extension->priv->prompt_entry);
        return TRUE;
}

static gboolean
gdm_fingerprint_extension_has_queued_messages (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);

        if (extension->priv->message_timeout_id != 0) {
                return TRUE;
        }

        if (!g_queue_is_empty (extension->priv->message_queue)) {
                return TRUE;
        }

        return FALSE;
}

static GIcon *
gdm_fingerprint_extension_get_icon (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        return g_object_ref (extension->priv->icon);
}

static char *
gdm_fingerprint_extension_get_name (GdmLoginExtension *extension)
{
        return g_strdup (_("Fingerprint Authentication"));
}

static char *
gdm_fingerprint_extension_get_description (GdmLoginExtension *extension)
{
        return g_strdup (_("Log into session with fingerprint"));
}

static gboolean
gdm_fingerprint_extension_is_choosable (GdmLoginExtension *extension)
{
        return FALSE;
}

static gboolean
gdm_fingerprint_extension_is_visible (GdmLoginExtension *login_extension)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (login_extension);
        GVariant *device_variant;
        char *contents, **lines;
        int i;

        if (!g_settings_get_boolean (extension->priv->settings, "enable-fingerprint-authentication")) {
                return FALSE;
        }

        if (extension->priv->bus_connection == NULL) {
                return FALSE;
        }

        device_variant =
            g_dbus_connection_call_sync (extension->priv->bus_connection,
                                         "net.reactivated.Fprint",
                                         "/net/reactivated/Fprint/Manager",
                                         "net.reactivated.Fprint.Manager",
                                         "GetDefaultDevice",
                                         NULL, G_VARIANT_TYPE_OBJECT_PATH,
                                         G_DBUS_CALL_FLAGS_NONE,
                                         -1,
                                         NULL,
                                         NULL);
        if (device_variant == NULL) {
                return FALSE;
        }

        g_variant_unref (device_variant);

        return TRUE;
}

static void
gdm_login_extension_iface_init (GdmLoginExtensionIface *iface)
{
        iface->get_icon = gdm_fingerprint_extension_get_icon;
        iface->get_description = gdm_fingerprint_extension_get_description;
        iface->get_name = gdm_fingerprint_extension_get_name;
        iface->is_choosable = gdm_fingerprint_extension_is_choosable;
        iface->is_visible = gdm_fingerprint_extension_is_visible;
        iface->queue_message = gdm_fingerprint_extension_queue_message;
        iface->ask_question = gdm_fingerprint_extension_ask_question;
        iface->ask_secret = gdm_fingerprint_extension_ask_secret;
        iface->reset = gdm_fingerprint_extension_reset;
        iface->set_ready = gdm_fingerprint_extension_set_ready;
        iface->get_service_name = gdm_fingerprint_extension_get_service_name;
        iface->get_page = gdm_fingerprint_extension_get_page;
        iface->get_actions = gdm_fingerprint_extension_get_actions;
        iface->request_answer = gdm_fingerprint_extension_request_answer;
        iface->focus = gdm_fingerprint_extension_focus;
        iface->has_queued_messages = gdm_fingerprint_extension_has_queued_messages;
}

static void
gdm_fingerprint_extension_class_init (GdmFingerprintExtensionClass *extension_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (extension_class);

        object_class->finalize = gdm_fingerprint_extension_finalize;

        g_type_class_add_private (extension_class,
                                  sizeof (GdmFingerprintExtensionPrivate));
}

static void
gdm_fingerprint_extension_finalize (GObject *object)
{
        GdmFingerprintExtension *extension = GDM_FINGERPRINT_EXTENSION (object);

        purge_message_queue (extension);

        if (extension->priv->bus_connection != NULL) {
                g_object_unref (extension->priv->bus_connection);
        }
}

static void
create_page (GdmFingerprintExtension *extension)
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
create_actions (GdmFingerprintExtension *extension)
{
        extension->priv->actions = gtk_action_group_new (GDM_FINGERPRINT_EXTENSION_NAME);
}

static void
gdm_fingerprint_extension_init (GdmFingerprintExtension *extension)
{
        extension->priv = G_TYPE_INSTANCE_GET_PRIVATE (extension,
                                                       GDM_TYPE_FINGERPRINT_EXTENSION,
                                                       GdmFingerprintExtensionPrivate);

        extension->priv->icon = g_themed_icon_new ("gdm-fingerprint");
        create_page (extension);
        create_actions (extension);

        extension->priv->message_queue = g_queue_new ();

        extension->priv->settings = g_settings_new ("org.gnome.login-screen");
        extension->priv->bus_connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, NULL);

        gdm_fingerprint_extension_reset (GDM_LOGIN_EXTENSION (extension));
}

void
g_io_module_load (GIOModule *module)
{
        g_io_extension_point_implement (GDM_LOGIN_EXTENSION_POINT_NAME,
                                        GDM_TYPE_FINGERPRINT_EXTENSION,
                                        GDM_FINGERPRINT_EXTENSION_NAME,
                                        0);
}

void
g_io_module_unload (GIOModule *module)
{
}
