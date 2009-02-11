/* gdm-smartcard-manager.h - object for monitoring smartcard insertion and
 *                           removal events
 *
 * Copyright (C) 2006, 2009 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * Written by: Ray Strode
 */
#ifndef GDM_SMARTCARD_MANAGER_H
#define GDM_SMARTCARD_MANAGER_H

#define GDM_SMARTCARD_ENABLE_INTERNAL_API
#include "gdm-smartcard.h"

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS
#define GDM_TYPE_SMARTCARD_MANAGER            (gdm_smartcard_manager_get_type ())
#define GDM_SMARTCARD_MANAGER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_SMARTCARD_MANAGER, GdmSmartcardManager))
#define GDM_SMARTCARD_MANAGER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_SMARTCARD_MANAGER, GdmSmartcardManagerClass))
#define GDM_IS_SMARTCARD_MANAGER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SC_TYPE_SMARTCARD_MANAGER))
#define GDM_IS_SMARTCARD_MANAGER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SC_TYPE_SMARTCARD_MANAGER))
#define GDM_SMARTCARD_MANAGER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_SMARTCARD_MANAGER, GdmSmartcardManagerClass))
#define GDM_SMARTCARD_MANAGER_ERROR           (gdm_smartcard_manager_error_quark ())
typedef struct _GdmSmartcardManager GdmSmartcardManager;
typedef struct _GdmSmartcardManagerClass GdmSmartcardManagerClass;
typedef struct _GdmSmartcardManagerPrivate GdmSmartcardManagerPrivate;
typedef enum _GdmSmartcardManagerError GdmSmartcardManagerError;

struct _GdmSmartcardManager {
    GObject parent;

    /*< private > */
    GdmSmartcardManagerPrivate *priv;
};

struct _GdmSmartcardManagerClass {
        GObjectClass parent_class;

        /* Signals */
        void (*smartcard_inserted) (GdmSmartcardManager *manager,
                                    GdmSmartcard        *token);
        void (*smartcard_removed) (GdmSmartcardManager *manager,
                                   GdmSmartcard        *token);
        void (*error) (GdmSmartcardManager *manager,
                       GError              *error);
};

enum _GdmSmartcardManagerError {
    GDM_SMARTCARD_MANAGER_ERROR_GENERIC = 0,
    GDM_SMARTCARD_MANAGER_ERROR_WITH_NSS,
    GDM_SMARTCARD_MANAGER_ERROR_LOADING_DRIVER,
    GDM_SMARTCARD_MANAGER_ERROR_WATCHING_FOR_EVENTS,
    GDM_SMARTCARD_MANAGER_ERROR_REPORTING_EVENTS
};

GType gdm_smartcard_manager_get_type (void) G_GNUC_CONST;
GQuark gdm_smartcard_manager_error_quark (void) G_GNUC_CONST;

GdmSmartcardManager *gdm_smartcard_manager_new (const char *module);

gboolean gdm_smartcard_manager_start (GdmSmartcardManager  *manager,
                                      GError              **error);

void gdm_smartcard_manager_stop (GdmSmartcardManager *manager);

char *gdm_smartcard_manager_get_module_path (GdmSmartcardManager *manager);
gboolean gdm_smartcard_manager_login_token_is_inserted (GdmSmartcardManager *manager);

G_END_DECLS
#endif                                /* GDM_SMARTCARD_MANAGER_H */
