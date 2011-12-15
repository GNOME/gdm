/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Authors: halton.huo@sun.com
 * Copyright (C) 2009 Sun Microsystems, Inc.
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

#include "config.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-common.h"
#include "gdm-display.h"
#include "gdm-dynamic-display.h"

#define GDM_DYNAMIC_DISPLAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_DYNAMIC_DISPLAY, GdmDynamicDisplayPrivate))

struct _GdmDynamicDisplayPrivate
{
        gboolean removed;
        gboolean do_respawn;
};

enum {
        PROP_0,
};

static void     gdm_dynamic_display_class_init   (GdmDynamicDisplayClass *klass);
static void     gdm_dynamic_display_init         (GdmDynamicDisplay      *display);
static void     gdm_dynamic_display_finalize     (GObject                  *object);

G_DEFINE_TYPE (GdmDynamicDisplay, gdm_dynamic_display, GDM_TYPE_DISPLAY)

void
gdm_dynamic_display_respawn (GdmDynamicDisplay *display, gboolean respawn)
{
        display->priv->do_respawn = respawn;
        if (display->priv->do_respawn == TRUE)
                g_debug ("GdmDynamicDisplay: Set respawn to TRUE.");
        else
                g_debug ("GdmDynamicDisplay: Set respawn to FALSE.");
}

void
gdm_dynamic_display_removed (GdmDynamicDisplay *display)
{
        display->priv->removed = TRUE;
}

static gboolean
gdm_dynamic_display_create_authority (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        GDM_DISPLAY_CLASS (gdm_dynamic_display_parent_class)->create_authority (display);

        return TRUE;
}

static gboolean
gdm_dynamic_display_add_user_authorization (GdmDisplay *display,
                                            const char *username,
                                            char      **filename,
                                            GError    **error)
{
        return GDM_DISPLAY_CLASS (gdm_dynamic_display_parent_class)->add_user_authorization (display, username, filename, error);
}

static gboolean
gdm_dynamic_display_remove_user_authorization (GdmDisplay *display,
                                               const char *username,
                                               GError    **error)
{
        return GDM_DISPLAY_CLASS (gdm_dynamic_display_parent_class)->remove_user_authorization (display, username, error);
}

static gboolean
gdm_dynamic_display_manage (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("GdmDynamicDisplay: Manage dynamic display");

        GDM_DISPLAY_CLASS (gdm_dynamic_display_parent_class)->manage (display);

        return TRUE;
}

static gboolean
gdm_dynamic_display_finish (GdmDisplay *display)
{
        int status;

        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("GdmDynamicDisplay: Finish dynamic display");

        /* Don't call parent's finish since we don't ever
        want to be put in the FINISHED state */

        /* restart dynamic displays */
        gdm_display_unmanage (display);

        status = gdm_display_get_status (display);
        if (GDM_DYNAMIC_DISPLAY (display)->priv->do_respawn == TRUE &&
            GDM_DYNAMIC_DISPLAY(display)->priv->removed == FALSE) {
                if (status != GDM_DISPLAY_FAILED) {
                        g_debug ("Respawning...");
                        gdm_display_manage (display);
                } else {
                        g_debug ("Display failed, not respawning...");
                }
        } else {
                g_debug ("Not respawning...");
        }
        GDM_DYNAMIC_DISPLAY (display)->priv->do_respawn = FALSE;

        return TRUE;
}

static gboolean
gdm_dynamic_display_unmanage (GdmDisplay *display)
{
        g_return_val_if_fail (GDM_IS_DISPLAY (display), FALSE);

        g_debug ("GdmDynamicDisplay: Unmanage dynamic display");

        GDM_DISPLAY_CLASS (gdm_dynamic_display_parent_class)->unmanage (display);

        return TRUE;
}

static void
gdm_dynamic_display_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_dynamic_display_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_dynamic_display_class_init (GdmDynamicDisplayClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayClass *display_class = GDM_DISPLAY_CLASS (klass);

        object_class->get_property = gdm_dynamic_display_get_property;
        object_class->set_property = gdm_dynamic_display_set_property;
        object_class->finalize = gdm_dynamic_display_finalize;

        display_class->create_authority = gdm_dynamic_display_create_authority;
        display_class->add_user_authorization = gdm_dynamic_display_add_user_authorization;
        display_class->remove_user_authorization = gdm_dynamic_display_remove_user_authorization;
        display_class->manage = gdm_dynamic_display_manage;
        display_class->finish = gdm_dynamic_display_finish;
        display_class->unmanage = gdm_dynamic_display_unmanage;

        g_type_class_add_private (klass, sizeof (GdmDynamicDisplayPrivate));
}

static void
gdm_dynamic_display_init (GdmDynamicDisplay *display)
{
        display->priv = GDM_DYNAMIC_DISPLAY_GET_PRIVATE (display);
        display->priv->removed = FALSE;
        display->priv->do_respawn = FALSE;
}

static void
gdm_dynamic_display_finalize (GObject *object)
{
        GdmDynamicDisplay *display;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_DYNAMIC_DISPLAY (object));

        g_debug ("GdmDynamicDisplay: Finalize dynamic display");

        display = GDM_DYNAMIC_DISPLAY (object);

        g_return_if_fail (display->priv != NULL);

        G_OBJECT_CLASS (gdm_dynamic_display_parent_class)->finalize (object);
}

GdmDisplay *
gdm_dynamic_display_new (int display_number)
{
        GObject *object;
        char    *x11_display;

        x11_display = g_strdup_printf (":%d", display_number);
        object = g_object_new (GDM_TYPE_DYNAMIC_DISPLAY,
                               "x11-display-number", display_number,
                               "x11-display-name", x11_display,
                               NULL);
        g_free (x11_display);

        return GDM_DISPLAY (object);
}
