/*
 * Copyright (C) Red Hat, Inc.
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
 * Written by: Ray Strode <rstrode@redhat.com>
 */


#ifndef __GDM_CONVERSATION_H
#define __GDM_CONVERSATION_H

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GDM_TYPE_CONVERSATION         (gdm_conversation_get_type ())
#define GDM_CONVERSATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_CONVERSATION, GdmConversation))
#define GDM_CONVERSATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_CONVERSATION, GdmConversationIface))
#define GDM_IS_CONVERSATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_CONVERSATION))
#define GDM_CONVERSATION_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GDM_TYPE_CONVERSATION, GdmConversationIface))

#define GDM_CONVERSATION_DEFAULT_ACTION "default-action"
#define GDM_CONVERSATION_OTHER_USER "__other"

typedef struct _GdmConversation      GdmConversation;
typedef struct _GdmConversationIface GdmConversationIface;

struct _GdmConversationIface
{
        GTypeInterface base_iface;

        /* methods */
        void   (* set_message)  (GdmConversation *conversation,
                                 const char      *message);
        void   (* ask_question) (GdmConversation *conversation,
                                 const char      *message);
        void   (* ask_secret)   (GdmConversation *conversation,
                                 const char      *message);
        void   (* reset)        (GdmConversation *conversation);
        void   (* set_ready)    (GdmConversation *conversation);
        char * (* get_service_name)  (GdmConversation *conversation);
        GtkWidget * (* get_page) (GdmConversation *conversation);
        GtkActionGroup * (* get_actions) (GdmConversation *conversation);
        void   (* request_answer)    (GdmConversation *conversation);
        gboolean   (* focus)    (GdmConversation *conversation);

        /* signals */
        char * (* answer)       (GdmConversation *conversation);
        void   (* cancel)       (GdmConversation *conversation);
        gboolean  (* user_chosen)  (GdmConversation *conversation);
};

GType  gdm_conversation_get_type     (void) G_GNUC_CONST;

void   gdm_conversation_set_message  (GdmConversation   *conversation,
                                      const char        *message);
void   gdm_conversation_ask_question (GdmConversation   *conversation,
                                      const char        *message);
void   gdm_conversation_ask_secret   (GdmConversation   *conversation,
                                      const char        *message);
void   gdm_conversation_reset        (GdmConversation   *converastion);
void   gdm_conversation_set_ready    (GdmConversation   *converastion);
char  *gdm_conversation_get_service_name   (GdmConversation   *conversation);
GtkWidget *gdm_conversation_get_page       (GdmConversation   *conversation);
GtkActionGroup *gdm_conversation_get_actions (GdmConversation   *conversation);
void   gdm_conversation_request_answer       (GdmConversation   *conversation);
gboolean   gdm_conversation_focus    (GdmConversation *conversation);

/* protected
 */
void   gdm_conversation_answer (GdmConversation   *conversation,
                                const char        *answer);
void   gdm_conversation_cancel (GdmConversation   *conversation);
gboolean  gdm_conversation_choose_user (GdmConversation   *conversation,
                                        const char        *username);

G_END_DECLS

#endif /* __GDM_CONVERSATION_H */
