/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Giovanni Campagna <scampa.giovanni@gmail.com>
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
 */

#ifndef __GDM_GREETER_CLIENT_H
#define __GDM_GREETER_CLIENT_H

#include <glib-object.h>
#include "gdm-client-glue.h"

G_BEGIN_DECLS

#define GDM_TYPE_GREETER_CLIENT         (gdm_greeter_client_get_type ())
#define GDM_GREETER_CLIENT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), GDM_TYPE_GREETER_CLIENT, GdmGreeterClient))
#define GDM_GREETER_CLIENT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), GDM_TYPE_GREETER_CLIENT, GdmGreeterClientClass))
#define GDM_IS_GREETER_CLIENT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), GDM_TYPE_GREETER_CLIENT))
#define GDM_IS_GREETER_CLIENT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), GDM_TYPE_GREETER_CLIENT))
#define GDM_GREETER_CLIENT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), GDM_TYPE_GREETER_CLIENT, GdmGreeterClientClass))

typedef struct GdmGreeterClientPrivate GdmGreeterClientPrivate;

typedef struct
{
        GObject                  parent;
        GdmGreeterClientPrivate *priv;
} GdmGreeterClient;

typedef struct
{
        GObjectClass   parent_class;

} GdmGreeterClientClass;

#define GDM_GREETER_CLIENT_ERROR (gdm_greeter_client_error_quark ())

typedef enum _GdmGreeterClientError {
        GDM_GREETER_CLIENT_ERROR_GENERIC = 0,
} GdmGreeterClientError;

GType              gdm_greeter_client_get_type                 (void);
GQuark             gdm_greeter_client_error_quark              (void);

GdmGreeterClient  *gdm_greeter_client_new                      (void);

void               gdm_greeter_client_open_reauthentication_channel (GdmGreeterClient     *client,
                                                                     const char           *username,
                                                                     GCancellable         *cancellable,
                                                                     GAsyncReadyCallback   callback,
                                                                     gpointer              user_data);

GdmUserVerifier   *gdm_greeter_client_open_reauthentication_channel_finish (GdmGreeterClient  *client,
                                                                            GAsyncResult      *result,
                                                                            GError           **error);

GdmUserVerifier   *gdm_greeter_client_open_reauthentication_channel_sync (GdmGreeterClient *client,
                                                                          const char       *username,
                                                                          GCancellable     *cancellable,
                                                                          GError          **error);

void               gdm_greeter_client_get_user_verifier         (GdmGreeterClient     *client,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
GdmUserVerifier   *gdm_greeter_client_get_user_verifier_finish  (GdmGreeterClient     *client,
                                                                 GAsyncResult         *result,
                                                                 GError              **error);
GdmUserVerifier   *gdm_greeter_client_get_user_verifier_sync    (GdmGreeterClient *client,
                                                                 GCancellable     *cancellable,
                                                                 GError          **error);

void               gdm_greeter_client_get_greeter               (GdmGreeterClient     *client,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
GdmGreeter        *gdm_greeter_client_get_greeter_finish        (GdmGreeterClient *client,
                                                                 GAsyncResult     *result,
                                                                 GError          **error);
GdmGreeter        *gdm_greeter_client_get_greeter_sync          (GdmGreeterClient *client,
                                                                 GCancellable     *cancellable,
                                                                 GError          **error);

void               gdm_greeter_client_get_remote_greeter        (GdmGreeterClient     *client,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
GdmRemoteGreeter  *gdm_greeter_client_get_remote_greeter_finish (GdmGreeterClient *client,
                                                                 GAsyncResult     *result,
                                                                 GError          **error);
GdmRemoteGreeter  *gdm_greeter_client_get_remote_greeter_sync   (GdmGreeterClient *client,
                                                                 GCancellable     *cancellable,
                                                                 GError          **error);

void               gdm_greeter_client_get_chooser               (GdmGreeterClient     *client,
                                                                 GCancellable         *cancellable,
                                                                 GAsyncReadyCallback   callback,
                                                                 gpointer              user_data);
GdmChooser        *gdm_greeter_client_get_chooser_finish        (GdmGreeterClient *client,
                                                                 GAsyncResult     *result,
                                                                 GError          **error);
GdmChooser        *gdm_greeter_client_get_chooser_sync          (GdmGreeterClient *client,
                                                                 GCancellable     *cancellable,
                                                                 GError          **error);


G_END_DECLS

#endif /* __GDM_GREETER_CLIENT_H */
