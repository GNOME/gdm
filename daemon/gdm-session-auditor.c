/* gdm-session-auditor.c - Object for auditing session login/logout
 *
 * Copyright (C) 2004, 2008 Sun Microsystems, Inc.
 * Copyright (C) 2005, 2008 Red Hat, Inc.
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Written by: Brian A. Cameron <Brian.Cameron@sun.com>
 *             Gary Winiger <Gary.Winiger@sun.com>
 *             Ray Strode <rstrode@redhat.com>
 *             Steve Grubb <sgrubb@redhat.com>
 */
#include "config.h"

#include "gdm-session-auditor.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <glib-object.h>
#include <glib.h>
#include <glib/gi18n.h>

typedef struct _GdmSessionAuditorPrivate
{
        char *username;
        char *hostname;
        char *display_device;
} GdmSessionAuditorPrivate;

static void gdm_session_auditor_finalize (GObject *object);

static void gdm_session_auditor_set_property (GObject      *object,
                                              unsigned int  prop_id,
                                              const GValue *value,
                                              GParamSpec   *pspec);
static void gdm_session_auditor_get_property (GObject      *object,
                                              unsigned int  prop_id,
                                              GValue       *value,
                                              GParamSpec   *pspec);

typedef enum {
        PROP_USERNAME = 1,
        PROP_HOSTNAME,
        PROP_DISPLAY_DEVICE,
} GdmSessionAuditorProps;

static GParamSpec *props[PROP_DISPLAY_DEVICE + 1] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GdmSessionAuditor, gdm_session_auditor, G_TYPE_OBJECT)

static void
gdm_session_auditor_class_init (GdmSessionAuditorClass *auditor_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (auditor_class);

        object_class->finalize = gdm_session_auditor_finalize;
        object_class->set_property = gdm_session_auditor_set_property;
        object_class->get_property = gdm_session_auditor_get_property;

        props[PROP_USERNAME] = g_param_spec_string ("username", NULL, NULL,
                                                    NULL, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);

        props[PROP_HOSTNAME] = g_param_spec_string ("hostname", NULL, NULL,
                                                    NULL,
                                                    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

        props[PROP_DISPLAY_DEVICE] = g_param_spec_string ("display-device", NULL, NULL,
                                                          NULL,
                                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

        g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gdm_session_auditor_init (GdmSessionAuditor *auditor)
{
}

static void
gdm_session_auditor_finalize (GObject *object)
{
        GdmSessionAuditor *auditor;
        GdmSessionAuditorPrivate *priv;
        GObjectClass *parent_class;

        auditor = GDM_SESSION_AUDITOR (object);
        priv = gdm_session_auditor_get_instance_private (auditor);

        g_clear_pointer (&priv->username, g_free);
        g_clear_pointer (&priv->hostname, g_free);
        g_clear_pointer (&priv->display_device, g_free);

        parent_class = G_OBJECT_CLASS (gdm_session_auditor_parent_class);

        if (parent_class->finalize != NULL) {
                parent_class->finalize (object);
        }
}

void
gdm_session_auditor_set_username (GdmSessionAuditor *auditor,
                                  const char        *username)
{
        GdmSessionAuditorPrivate *priv;

        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        priv = gdm_session_auditor_get_instance_private (auditor);

        if (username == priv->username) {
                return;
        }

        if ((username == NULL || priv->username == NULL) ||
            strcmp (username, priv->username) != 0) {
                priv->username = g_strdup (username);
                g_object_notify_by_pspec (G_OBJECT (auditor), props[PROP_USERNAME]);
        }
}

static void
gdm_session_auditor_set_hostname (GdmSessionAuditor *auditor,
                                  const char        *hostname)
{
        GdmSessionAuditorPrivate *priv;

        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        priv = gdm_session_auditor_get_instance_private (auditor);
        priv->hostname = g_strdup (hostname);
}

static void
gdm_session_auditor_set_display_device (GdmSessionAuditor *auditor,
                                        const char        *display_device)
{
        GdmSessionAuditorPrivate *priv;

        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        priv = gdm_session_auditor_get_instance_private (auditor);
        priv->display_device = g_strdup (display_device);
}

static char *
gdm_session_auditor_get_username (GdmSessionAuditor *auditor)
{
        GdmSessionAuditorPrivate *priv;

        priv = gdm_session_auditor_get_instance_private (auditor);
        return g_strdup (priv->username);
}

static char *
gdm_session_auditor_get_hostname (GdmSessionAuditor *auditor)
{
        GdmSessionAuditorPrivate *priv;

        priv = gdm_session_auditor_get_instance_private (auditor);
        return g_strdup (priv->hostname);
}

static char *
gdm_session_auditor_get_display_device (GdmSessionAuditor *auditor)
{
        GdmSessionAuditorPrivate *priv;

        priv = gdm_session_auditor_get_instance_private (auditor);
        return g_strdup (priv->display_device);
}

static void
gdm_session_auditor_set_property (GObject      *object,
                                  unsigned int  prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
        GdmSessionAuditor *auditor;

        auditor = GDM_SESSION_AUDITOR (object);

        switch ((GdmSessionAuditorProps) prop_id) {
                case PROP_USERNAME:
                        gdm_session_auditor_set_username (auditor, g_value_get_string (value));
                break;

                case PROP_HOSTNAME:
                        gdm_session_auditor_set_hostname (auditor, g_value_get_string (value));
                break;

                case PROP_DISPLAY_DEVICE:
                        gdm_session_auditor_set_display_device (auditor, g_value_get_string (value));
                break;
        }
}

static void
gdm_session_auditor_get_property (GObject      *object,
                                  unsigned int  prop_id,
                                  GValue       *value,
                                  GParamSpec   *pspec)
{
        GdmSessionAuditor *auditor;

        auditor = GDM_SESSION_AUDITOR (object);

        switch ((GdmSessionAuditorProps) prop_id) {
                case PROP_USERNAME:
                        g_value_take_string (value, gdm_session_auditor_get_username (auditor));
                break;

                case PROP_HOSTNAME:
                        g_value_take_string (value, gdm_session_auditor_get_hostname (auditor));
                break;

                case PROP_DISPLAY_DEVICE:
                        g_value_take_string (value, gdm_session_auditor_get_display_device (auditor));
                break;
    }
}

GdmSessionAuditor *
gdm_session_auditor_new (const char *hostname,
                         const char *display_device)
{
        GdmSessionAuditor *auditor;

        auditor = g_object_new (GDM_TYPE_SESSION_AUDITOR,
                                "hostname", hostname,
                                "display-device", display_device,
                                NULL);

        return auditor;
}

void
gdm_session_auditor_report_password_changed (GdmSessionAuditor *auditor)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_password_changed != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_password_changed (auditor);
        }
}

void
gdm_session_auditor_report_password_change_failure (GdmSessionAuditor *auditor)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_password_change_failure != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_password_change_failure (auditor);
        }
}

void
gdm_session_auditor_report_user_accredited (GdmSessionAuditor *auditor)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_user_accredited != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_user_accredited (auditor);
        }
}

void
gdm_session_auditor_report_login (GdmSessionAuditor *auditor)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_login != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_login (auditor);
        }
}

void
gdm_session_auditor_report_login_failure (GdmSessionAuditor *auditor,
                                          int                error_code,
                                          const char        *error_message)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_login_failure != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_login_failure (auditor, error_code, error_message);
        }
}

void
gdm_session_auditor_report_logout (GdmSessionAuditor *auditor)
{
        g_return_if_fail (GDM_IS_SESSION_AUDITOR (auditor));

        if (GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_logout != NULL) {
                GDM_SESSION_AUDITOR_GET_CLASS (auditor)->report_logout (auditor);
        }
}
