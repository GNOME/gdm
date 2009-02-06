/* securitycard.h - api for reading and writing data to a security card
 *
 * Copyright (C) 2006 Ray Strode
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
 */
#ifndef GDM_SMARTCARD_H
#define GDM_SMARTCARD_H

#include <glib.h>
#include <glib-object.h>

#include <secmod.h>

G_BEGIN_DECLS
#define GDM_TYPE_SMARTCARD            (gdm_smartcard_get_type ())
#define GDM_SMARTCARD(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GDM_TYPE_SMARTCARD, GdmSmartcard))
#define GDM_SMARTCARD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_SMARTCARD, GdmSmartcardClass))
#define GDM_IS_SMARTCARD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GDM_TYPE_SMARTCARD))
#define GDM_IS_SMARTCARD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_SMARTCARD))
#define GDM_SMARTCARD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GDM_TYPE_SMARTCARD, GdmSmartcardClass))
#define GDM_SMARTCARD_ERROR           (gdm_smartcard_error_quark ())
typedef struct _GdmSmartcardClass GdmSmartcardClass;
typedef struct _GdmSmartcard GdmSmartcard;
typedef struct _GdmSmartcardPrivate GdmSmartcardPrivate;
typedef enum _GdmSmartcardError GdmSmartcardError;
typedef enum _GdmSmartcardState GdmSmartcardState;

typedef struct _GdmSmartcardRequest GdmSmartcardRequest;

struct _GdmSmartcard {
    GObject parent;

    /*< private > */
    GdmSmartcardPrivate *priv;
};

struct _GdmSmartcardClass {
    GObjectClass parent_class;

    void (* inserted) (GdmSmartcard *card);
    void (* removed)  (GdmSmartcard *card);
};

enum _GdmSmartcardError {
    GDM_SMARTCARD_ERROR_GENERIC = 0,
};

enum _GdmSmartcardState {
    GDM_SMARTCARD_STATE_INSERTED = 0,
    GDM_SMARTCARD_STATE_REMOVED,
};

GType gdm_smartcard_get_type (void) G_GNUC_CONST;
GQuark gdm_smartcard_error_quark (void) G_GNUC_CONST;

CK_SLOT_ID gdm_smartcard_get_slot_id (GdmSmartcard *card);
gint gdm_smartcard_get_slot_series (GdmSmartcard *card);
GdmSmartcardState gdm_smartcard_get_state (GdmSmartcard *card);

char *gdm_smartcard_get_name (GdmSmartcard *card);
gboolean gdm_smartcard_is_login_card (GdmSmartcard *card);

gboolean gdm_smartcard_unlock (GdmSmartcard *card,
                               const char   *password);

/* don't under any circumstances call these functions */
#ifdef GDM_SMARTCARD_ENABLE_INTERNAL_API

GdmSmartcard *_gdm_smartcard_new (SECMODModule *module,
                                  CK_SLOT_ID    slot_id,
                                  gint          slot_series);
GdmSmartcard *_gdm_smartcard_new_from_name (SECMODModule *module,
                                            const char   *name);

void _gdm_smartcard_set_state (GdmSmartcard      *card,
                               GdmSmartcardState  state);
#endif

G_END_DECLS
#endif                                /* GDM_SMARTCARD_H */
