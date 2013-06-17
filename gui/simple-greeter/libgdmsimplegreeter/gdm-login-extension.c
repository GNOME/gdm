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

#include <glib.h>
#include <glib-object.h>

#include "gdm-login-extension.h"

enum {
        ENABLED,
        DISABLED,
        ANSWER,
        USER_CHOSEN,
        CANCEL,
        MESSAGE_QUEUE_EMPTY,
        NUMBER_OF_SIGNALS
};

static guint signals [NUMBER_OF_SIGNALS] = { 0, };

static void gdm_login_extension_class_init (gpointer g_iface);

GType
gdm_login_extension_get_type (void)
{
        static GType login_extension_type = 0;

        if (!login_extension_type) {
                login_extension_type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                           "GdmLoginExtension",
                                                           sizeof (GdmLoginExtensionIface),
                                                           (GClassInitFunc) gdm_login_extension_class_init,
                                                           0, NULL, 0);

                g_type_interface_add_prerequisite (login_extension_type, G_TYPE_OBJECT);
        }

        return login_extension_type;
}

static void
gdm_login_extension_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [ENABLED] =
                g_signal_new ("enabled",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, enabled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [DISABLED] =
                g_signal_new ("disabled",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, disabled),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        signals [ANSWER] =
                g_signal_new ("answer",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [USER_CHOSEN] =
                g_signal_new ("user-chosen",
                              iface_type,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, user_chosen),
                              NULL,
                              NULL,
                              g_cclosure_marshal_generic,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_STRING);
        signals [CANCEL] =
                g_signal_new ("cancel",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, cancel),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [MESSAGE_QUEUE_EMPTY] =
                g_signal_new ("message-queue-empty",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmLoginExtensionIface, message_queue_empty),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

GIcon *
gdm_login_extension_get_icon (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_icon (extension);
}

char *
gdm_login_extension_get_description (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_description (extension);
}

char *
gdm_login_extension_get_name (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_name (extension);
}

void
gdm_login_extension_set_enabled (GdmLoginExtension *extension,
                                   gboolean             should_enable)
{
        g_object_set_data (G_OBJECT (extension),
                           "gdm-greeter-extension-is-disabled",
                           GINT_TO_POINTER (!should_enable));

        if (should_enable) {
                g_signal_emit (G_OBJECT (extension), signals [ENABLED], 0);
        } else {
                g_signal_emit (G_OBJECT (extension), signals [DISABLED], 0);
        }
}

gboolean
gdm_login_extension_is_enabled (GdmLoginExtension *extension)
{
        return !g_object_get_data (G_OBJECT (extension), "gdm-greeter-extension-is-disabled");
}

gboolean
gdm_login_extension_is_choosable (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->is_choosable (extension);
}

gboolean
gdm_login_extension_is_visible (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->is_visible (extension);
}

void
gdm_login_extension_queue_message (GdmLoginExtension   *extension,
                                     GdmServiceMessageType  type,
                                     const char            *message)
{
        GDM_LOGIN_EXTENSION_GET_IFACE (extension)->queue_message (extension,
                                                                    type,
                                                                    message);
}

void
gdm_login_extension_ask_question (GdmLoginExtension *extension,
                                    const char          *message)
{
        GDM_LOGIN_EXTENSION_GET_IFACE (extension)->ask_question (extension,
                                                                   message);
}

void
gdm_login_extension_ask_secret (GdmLoginExtension *extension,
                                  const char          *message)
{
        GDM_LOGIN_EXTENSION_GET_IFACE (extension)->ask_secret (extension,
                                                                 message);

}

void
gdm_login_extension_reset (GdmLoginExtension *extension)
{
        GDM_LOGIN_EXTENSION_GET_IFACE (extension)->reset (extension);
}

void
gdm_login_extension_set_ready (GdmLoginExtension *extension)
{
        GDM_LOGIN_EXTENSION_GET_IFACE (extension)->set_ready (extension);
}

gboolean
gdm_login_extension_focus (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->focus (extension);
}

char *
gdm_login_extension_get_service_name (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_service_name (extension);

}

gboolean
gdm_login_extension_has_queued_messages (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->has_queued_messages (extension);
}

GtkWidget *
gdm_login_extension_get_page (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_page (extension);
}

GtkActionGroup *
gdm_login_extension_get_actions (GdmLoginExtension *extension)
{
        return GDM_LOGIN_EXTENSION_GET_IFACE (extension)->get_actions (extension);
}

void
_gdm_login_extension_emit_answer (GdmLoginExtension *extension,
                                  const char        *answer)
{
        g_signal_emit (extension, signals [ANSWER], 0, answer);

}

void
_gdm_login_extension_emit_cancel (GdmLoginExtension *extension)
{
        g_signal_emit (extension, signals [CANCEL], 0);
}

gboolean
_gdm_login_extension_emit_choose_user (GdmLoginExtension *extension,
                                     const char          *username)
{
        gboolean was_chosen;

        was_chosen = FALSE;

        g_signal_emit (extension, signals [USER_CHOSEN], 0, username, &was_chosen);

        return was_chosen;
}

void
_gdm_login_extension_emit_message_queue_empty (GdmLoginExtension *extension)
{
        g_signal_emit (extension, signals [MESSAGE_QUEUE_EMPTY], 0);

}
