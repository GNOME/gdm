/*
 * Copyright 2009 Red Hat, Inc.  *
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
 * Written by: Ray Strode
 */

#ifndef __GDM_LOGIN_EXTENSION_H
#define __GDM_LOGIN_EXTENSION_H

#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GDM_TYPE_LOGIN_EXTENSION         (gdm_login_extension_get_type ())
#define GDM_LOGIN_EXTENSION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_LOGIN_EXTENSION, GdmLoginExtension))
#define GDM_LOGIN_EXTENSION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_LOGIN_EXTENSION, GdmLoginExtensionClass))
#define GDM_IS_LOGIN_EXTENSION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_LOGIN_EXTENSION))
#define GDM_LOGIN_EXTENSION_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), GDM_TYPE_LOGIN_EXTENSION, GdmLoginExtensionIface))

#define GDM_LOGIN_EXTENSION_POINT_NAME "gdm-login"
#define GDM_LOGIN_EXTENSION_DEFAULT_ACTION "default-action"
#define GDM_LOGIN_EXTENSION_OTHER_USER "__other"

typedef struct _GdmLoginExtension      GdmLoginExtension;
typedef struct _GdmLoginExtensionIface GdmLoginExtensionIface;

typedef enum {
        GDM_SERVICE_MESSAGE_TYPE_INFO,
        GDM_SERVICE_MESSAGE_TYPE_PROBLEM
} GdmServiceMessageType;

struct _GdmLoginExtensionIface
{
        GTypeInterface base_iface;

        /* methods */
        GIcon *         (* get_icon)             (GdmLoginExtension   *extension);
        char *          (* get_description)      (GdmLoginExtension   *extension);
        char *          (* get_name)             (GdmLoginExtension   *extension);

        gboolean        (* is_choosable)         (GdmLoginExtension   *extension);
        gboolean        (* is_visible)           (GdmLoginExtension   *extension);

        void            (* queue_message)        (GdmLoginExtension   *extension,
                                                  GdmServiceMessageType  type,
                                                  const char            *message);
        void            (* ask_question)         (GdmLoginExtension   *extension,
                                                  const char            *message);
        void            (* ask_secret)           (GdmLoginExtension   *extension,
                                                  const char            *message);
        void             (* reset)               (GdmLoginExtension   *extension);
        void             (* set_ready)           (GdmLoginExtension   *extension);
        char *           (* get_service_name)    (GdmLoginExtension   *extension);
        GtkWidget *      (* get_page)            (GdmLoginExtension   *extension);
        GtkActionGroup * (* get_actions)         (GdmLoginExtension   *extension);
        void             (* request_answer)      (GdmLoginExtension   *extension);
        gboolean         (* has_queued_messages) (GdmLoginExtension   *extension);
        gboolean         (* focus)               (GdmLoginExtension   *extension);

        /* signals */
        void            (* enabled)              (GdmLoginExtension   *extension);
        void            (* disabled)             (GdmLoginExtension   *extension);
        char *          (* answer)               (GdmLoginExtension   *extension);
        void            (* cancel)               (GdmLoginExtension   *extension);
        gboolean        (* user_chosen)          (GdmLoginExtension   *extension);
        gboolean        (* message_queue_empty)  (GdmLoginExtension   *extension);
};

GType    gdm_login_extension_get_type        (void) G_GNUC_CONST;

GIcon   *gdm_login_extension_get_icon        (GdmLoginExtension   *extension);
char    *gdm_login_extension_get_description (GdmLoginExtension   *extension);
char    *gdm_login_extension_get_name        (GdmLoginExtension   *extension);
void     gdm_login_extension_set_enabled     (GdmLoginExtension   *extension,
                                              gboolean               should_enable);
gboolean gdm_login_extension_is_enabled      (GdmLoginExtension   *extension);
gboolean gdm_login_extension_is_choosable    (GdmLoginExtension   *extension);
gboolean gdm_login_extension_is_visible      (GdmLoginExtension   *extension);

void   gdm_login_extension_queue_message  (GdmLoginExtension   *extension,
                                           GdmServiceMessageType  type,
                                           const char            *message);

void   gdm_login_extension_ask_question (GdmLoginExtension   *extension,
                                         const char            *message);

void   gdm_login_extension_ask_secret   (GdmLoginExtension   *extension,
                                         const char            *message);

void     gdm_login_extension_reset        (GdmLoginExtension *extension);
void     gdm_login_extension_set_ready    (GdmLoginExtension *extension);
gboolean gdm_login_extension_focus        (GdmLoginExtension *extension);

char     *gdm_login_extension_get_service_name    (GdmLoginExtension *extension);
gboolean  gdm_login_extension_has_queued_messages (GdmLoginExtension *extension);

GtkWidget      *gdm_login_extension_get_page    (GdmLoginExtension *extension);
GtkActionGroup *gdm_login_extension_get_actions (GdmLoginExtension *extension);


/* protected
 */
void      _gdm_login_extension_emit_answer           (GdmLoginExtension *extension,
                                                      const char          *answer);
void      _gdm_login_extension_emit_cancel           (GdmLoginExtension *extension);
gboolean  _gdm_login_extension_emit_choose_user      (GdmLoginExtension *extension,
                                                      const char          *username);

void      _gdm_login_extension_emit_message_queue_empty (GdmLoginExtension *extension);


G_END_DECLS
#endif /* __GDM_LOGIN_EXTENSION_H */
