/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
 */


#ifndef __GDM_DISPLAY_H
#define __GDM_DISPLAY_H

#include <glib-object.h>
#include <gio/gio.h>

#include "gdm-dynamic-user-store.h"

G_BEGIN_DECLS

#define GDM_TYPE_DISPLAY (gdm_display_get_type ())
G_DECLARE_DERIVABLE_TYPE (GdmDisplay, gdm_display, GDM, DISPLAY, GObject)

typedef enum {
        GDM_DISPLAY_UNMANAGED = 0,
        GDM_DISPLAY_PREPARED,
        GDM_DISPLAY_MANAGED,
        GDM_DISPLAY_WAITING_TO_FINISH,
        GDM_DISPLAY_FINISHED,
        GDM_DISPLAY_FAILED,
} GdmDisplayStatus;

struct _GdmDisplayClass
{
        GObjectClass   parent_class;

        /* methods */
        gboolean (*prepare) (GdmDisplay *display);
        void     (*manage)  (GdmDisplay *self);
};

typedef enum
{
         GDM_DISPLAY_ERROR_GENERAL,
         GDM_DISPLAY_ERROR_GETTING_USER_INFO,
         GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
} GdmDisplayError;

#define GDM_DISPLAY_ERROR gdm_display_error_quark ()

GQuark              gdm_display_error_quark                    (void);

int                 gdm_display_get_status                     (GdmDisplay *display);
time_t              gdm_display_get_creation_time              (GdmDisplay *display);
const char *        gdm_display_get_session_id                 (GdmDisplay *display);
gboolean            gdm_display_create_authority               (GdmDisplay *display);
gboolean            gdm_display_prepare                        (GdmDisplay *display);
gboolean            gdm_display_manage                         (GdmDisplay *display);
gboolean            gdm_display_finish                         (GdmDisplay *display);
gboolean            gdm_display_unmanage                       (GdmDisplay *display);

GDBusObjectSkeleton *gdm_display_get_object_skeleton           (GdmDisplay *display);

/* exported to bus */
gboolean            gdm_display_get_id                         (GdmDisplay *display,
                                                                char      **id,
                                                                GError    **error);
gboolean            gdm_display_get_remote_hostname            (GdmDisplay *display,
                                                                char      **hostname,
                                                                GError    **error);
gboolean            gdm_display_get_x11_display_number         (GdmDisplay *display,
                                                                int        *number,
                                                                GError    **error);
gboolean            gdm_display_get_x11_display_name           (GdmDisplay *display,
                                                                char      **x11_display,
                                                                GError    **error);
gboolean            gdm_display_get_seat_id                    (GdmDisplay *display,
                                                                char      **seat_id,
                                                                GError    **error);
gboolean            gdm_display_is_local                       (GdmDisplay *display,
                                                                gboolean   *local,
                                                                GError    **error);
gboolean            gdm_display_is_initial                     (GdmDisplay  *display,
                                                                gboolean    *initial,
                                                                GError     **error);

gboolean            gdm_display_get_x11_cookie                 (GdmDisplay  *display,
                                                                const char **x11_cookie,
                                                                gsize       *x11_cookie_size,
                                                                GError     **error);
gboolean            gdm_display_get_x11_authority_file         (GdmDisplay *display,
                                                                char      **filename,
                                                                GError    **error);
gboolean            gdm_display_add_user_authorization         (GdmDisplay *display,
                                                                const char *username,
                                                                char      **filename,
                                                                GError    **error);
gboolean            gdm_display_remove_user_authorization      (GdmDisplay *display,
                                                                const char *username,
                                                                GError    **error);
gboolean            gdm_display_prepare_greeter_session        (GdmDisplay          *display,
                                                                GdmDynamicUserStore *dyn_user_store,
                                                                uid_t               *ret_uid);
void                gdm_display_start_greeter_session          (GdmDisplay  *display);
void                gdm_display_stop_greeter_session           (GdmDisplay  *display);

gboolean            gdm_display_connect                        (GdmDisplay *self);

G_END_DECLS

#endif /* __GDM_DISPLAY_H */
