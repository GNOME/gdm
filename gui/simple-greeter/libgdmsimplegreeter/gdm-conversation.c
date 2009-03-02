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

#include <glib.h>
#include <glib-object.h>

#include <gtk/gtk.h>

#include "gdm-conversation.h"
#include "gdm-marshal.h"
#include "gdm-task.h"

enum {
        ANSWER,
        USER_CHOSEN,
        CANCEL,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void gdm_conversation_class_init (gpointer g_iface);

GType
gdm_conversation_get_type (void)
{
        static GType type = 0;

        if (!type) {
                type = g_type_register_static_simple (G_TYPE_INTERFACE,
                                                      "GdmConversation",
                                                      sizeof (GdmConversationIface),
                                                      (GClassInitFunc) gdm_conversation_class_init,
                                                      0, NULL, 0);

                g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
                g_type_interface_add_prerequisite (type, GDM_TYPE_TASK);
        }

        return type;
}

static void
gdm_conversation_class_init (gpointer g_iface)
{
        GType iface_type = G_TYPE_FROM_INTERFACE (g_iface);

        signals [ANSWER] =
                g_signal_new ("answer",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmConversationIface, answer),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [USER_CHOSEN] =
                g_signal_new ("user-chosen",
                              iface_type,
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmConversationIface, user_chosen),
                              NULL,
                              NULL,
                              gdm_marshal_BOOLEAN__STRING,
                              G_TYPE_BOOLEAN,
                              1, G_TYPE_STRING);
        signals [CANCEL] =
                g_signal_new ("cancel",
                              iface_type,
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmConversationIface, cancel),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

void
gdm_conversation_set_message  (GdmConversation   *conversation,
                               const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->set_message (conversation, message);
}

void
gdm_conversation_ask_question (GdmConversation   *conversation,
                               const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->ask_question (conversation, message);
}

void
gdm_conversation_ask_secret (GdmConversation   *conversation,
                             const char        *message)
{
        GDM_CONVERSATION_GET_IFACE (conversation)->ask_secret (conversation, message);
}

void
gdm_conversation_reset (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->reset (conversation);
}

void
gdm_conversation_set_ready (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->set_ready (conversation);
}

char *
gdm_conversation_get_service_name (GdmConversation   *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_service_name (conversation);
}

GtkWidget *
gdm_conversation_get_page (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_page (conversation);
}

GtkActionGroup *
gdm_conversation_get_actions (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->get_actions (conversation);
}

gboolean
gdm_conversation_focus (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->focus (conversation);
}

void
gdm_conversation_request_answer (GdmConversation *conversation)
{
        return GDM_CONVERSATION_GET_IFACE (conversation)->request_answer (conversation);
}

/* protected
 */
void
gdm_conversation_answer (GdmConversation   *conversation,
                         const char        *answer)
{
        g_signal_emit (conversation, signals [ANSWER], 0, answer);
}

void
gdm_conversation_cancel (GdmConversation   *conversation)
{
        g_signal_emit (conversation, signals [CANCEL], 0);
}

gboolean
gdm_conversation_choose_user (GdmConversation *conversation,
                              const char      *username)
{
        gboolean was_chosen;

        was_chosen = FALSE;

        g_signal_emit (conversation, signals [USER_CHOSEN], 0, username, &was_chosen);

        return was_chosen;
}
