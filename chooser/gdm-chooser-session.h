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

#ifndef __GDM_CHOOSER_SESSION_H
#define __GDM_CHOOSER_SESSION_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GDM_TYPE_CHOOSER_SESSION (gdm_chooser_session_get_type ())
G_DECLARE_FINAL_TYPE (GdmChooserSession, gdm_chooser_session, GDM, CHOOSER_SESSION, GObject)

GdmChooserSession    * gdm_chooser_session_new                            (void);

gboolean               gdm_chooser_session_start                          (GdmChooserSession *session,
                                                                           GError           **error);
void                   gdm_chooser_session_stop                           (GdmChooserSession *session);

G_END_DECLS

#endif /* __GDM_CHOOSER_SESSION_H */
