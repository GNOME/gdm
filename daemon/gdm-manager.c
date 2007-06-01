/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "gdm-manager.h"
#include "gdm-manager-glue.h"
#include "gdm-display-store.h"
#include "gdm-xdmcp-manager.h"
#include "gdm-common.h"

#include "gdm-static-display.h"

#define GDM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_MANAGER, GdmManagerPrivate))

#define GDM_DBUS_PATH         "/org/gnome/DisplayManager"
#define GDM_MANAGER_DBUS_PATH GDM_DBUS_PATH "/Manager"
#define GDM_MANAGER_DBUS_NAME "org.gnome.DisplayManager.Manager"

struct GdmManagerPrivate
{
	GdmDisplayStore *display_store;
	GdmXdmcpManager *xdmcp_manager;

	gboolean         xdmcp_enabled;

	GString         *global_cookie;
	gboolean         wait_for_go;
	gboolean         no_console;

        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;
};

enum {
	PROP_0,
	PROP_XDMCP_ENABLED
};

enum {
	DISPLAY_ADDED,
	DISPLAY_REMOVED,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void	gdm_manager_class_init	(GdmManagerClass *klass);
static void	gdm_manager_init	(GdmManager	 *manager);
static void	gdm_manager_finalize	(GObject	 *object);

static gpointer manager_object = NULL;

G_DEFINE_TYPE (GdmManager, gdm_manager, G_TYPE_OBJECT)

GQuark
gdm_manager_error_quark (void)
{
	static GQuark ret = 0;
	if (ret == 0) {
		ret = g_quark_from_static_string ("gdm_manager_error");
	}

	return ret;
}

static gboolean
listify_display_ids (const char *id,
		     GdmDisplay *display,
		     GPtrArray **array)
{
	g_ptr_array_add (*array, g_strdup (id));

	/* return FALSE to continue */
	return FALSE;
}

/*
  Example:
  dbus-send --system --dest=org.gnome.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/DisplayManager/Manager \
  org.gnome.DisplayManager.Manager.GetDisplays
*/
gboolean
gdm_manager_get_displays (GdmManager *manager,
			  GPtrArray **displays,
			  GError    **error)
{
	g_return_val_if_fail (GDM_IS_MANAGER (manager), FALSE);

	if (displays == NULL) {
		return FALSE;
	}

	*displays = g_ptr_array_new ();
	gdm_display_store_foreach (manager->priv->display_store,
				   (GdmDisplayStoreFunc)listify_display_ids,
				   displays);

	return TRUE;
}

static gboolean
start_local_display (const char *id,
		     GdmDisplay *d,
		     GdmManager *manager)
{
	gboolean ret;

	ret = TRUE;

	g_assert (d != NULL);

	if (GDM_IS_STATIC_DISPLAY (d) &&
	    gdm_display_get_status (d) == GDM_DISPLAY_UNMANAGED) {
		if (! gdm_display_manage (d)) {
			gdm_display_unmanage (d);
		} else {
			ret = FALSE;
		}
	}

	return ret;
}

static void
start_unborn_local_displays (GdmManager *manager)
{
	gdm_display_store_foreach (manager->priv->display_store,
				   (GdmDisplayStoreFunc)start_local_display,
				   manager);
}

static void
make_global_cookie (GdmManager *manager)
{
	FILE  *fp;
	char  *file;

	gdm_generate_cookie (manager->priv->global_cookie);

	file = g_build_filename (AUTHDIR, ".cookie", NULL);
	VE_IGNORE_EINTR (g_unlink (file));

	fp = gdm_safe_fopen_w (file, 077);
	if G_UNLIKELY (fp == NULL) {
		g_warning (_("Can't open %s for writing"), file);
		g_free (file);
		return;
	}

	VE_IGNORE_EINTR (fprintf (fp, "%s\n", manager->priv->global_cookie->str));

	/* FIXME: What about out of disk space errors? */
	errno = 0;
	VE_IGNORE_EINTR (fclose (fp));
	if G_UNLIKELY (errno != 0) {
		g_warning (_("Can't write to %s: %s"),
			   file,
			   g_strerror (errno));
	}

	g_free (file);
}

static void
load_static_displays_from_file (GdmManager *manager)
{
#if 0

	for (l = xservers; l != NULL; l = l->next) {
		GdmDisplay *display;

		g_debug ("Loading display for '%d' %s", xserver->number, xserver->id);

		display = gdm_static_display_new (xserver->number);

		if (display == NULL) {
			g_warning ("Unable to create display: %d %s", xserver->number, xserver->id);
			continue;
		}

		gdm_display_store_add (manager->priv->display_store, display);

		/* let store own the ref */
		g_object_unref (display);
	}
#endif
}

static void
load_static_servers (GdmManager *manager)
{

	load_static_displays_from_file (manager);
}

void
gdm_manager_start (GdmManager *manager)
{
	g_debug ("GDM starting to manage");

	load_static_servers (manager);

	/* Start static X servers */
	start_unborn_local_displays (manager);

	/* Accept remote connections */
	if (manager->priv->xdmcp_enabled && ! manager->priv->wait_for_go) {
		if (manager->priv->xdmcp_manager != NULL) {
			g_debug ("Accepting XDMCP connections...");
			gdm_xdmcp_manager_start (manager->priv->xdmcp_manager, NULL);
		}
	}

}

void
gdm_manager_set_wait_for_go (GdmManager *manager,
			     gboolean    wait_for_go)
{
	if (manager->priv->wait_for_go != wait_for_go) {
		manager->priv->wait_for_go = wait_for_go;

		if (! wait_for_go) {
			/* we got a go */

			if (manager->priv->xdmcp_enabled && manager->priv->xdmcp_manager != NULL) {
				g_debug ("Accepting XDMCP connections...");
				gdm_xdmcp_manager_start (manager->priv->xdmcp_manager, NULL);
			}
		}
	}
}

typedef struct {
        const char *service_name;
        GdmManager *manager;
} RemoveDisplayData;

static gboolean
remove_display_for_connection (GdmDisplay        *display,
			       RemoveDisplayData *data)
{
        g_assert (display != NULL);
        g_assert (data->service_name != NULL);

	/* FIXME: compare service name to that of display */
#if 0
        if (strcmp (info->service_name, data->service_name) == 0) {
                remove_session_for_cookie (data->manager, cookie, NULL);
                leader_info_cancel (info);
                return TRUE;
        }
#endif

        return FALSE;
}

static void
remove_displays_for_connection (GdmManager *manager,
                                const char *service_name)
{
        RemoveDisplayData data;

        data.service_name = service_name;
        data.manager = manager;

        g_debug ("Removing display for service name: %s", service_name);

	gdm_display_store_foreach_remove (manager->priv->display_store,
					  (GdmDisplayStoreFunc)remove_display_for_connection,
					  &data);
}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        GdmManager  *manager)
{
        if (strlen (new_service_name) == 0) {
                remove_displays_for_connection (manager, old_service_name);
        }

        g_debug ("NameOwnerChanged: service_name='%s', old_service_name='%s' new_service_name='%s'",
                   service_name, old_service_name, new_service_name);
}

static gboolean
register_manager (GdmManager *manager)
{
        GError *error = NULL;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        manager->priv->bus_proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);
        dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                 "NameOwnerChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                     "NameOwnerChanged",
                                     G_CALLBACK (bus_name_owner_changed),
                                     manager,
                                     NULL);

        dbus_g_connection_register_g_object (manager->priv->connection, GDM_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

void
gdm_manager_set_xdmcp_enabled (GdmManager *manager,
			       gboolean    enabled)
{
	g_return_if_fail (GDM_IS_MANAGER (manager));

	manager->priv->xdmcp_enabled = enabled;
}

static void
gdm_manager_set_property (GObject      *object,
			  guint	        prop_id,
			  const GValue  *value,
			  GParamSpec    *pspec)
{
	GdmManager *self;

	self = GDM_MANAGER (object);

	switch (prop_id) {
	case PROP_XDMCP_ENABLED:
		gdm_manager_set_xdmcp_enabled (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gdm_manager_get_property (GObject    *object,
			  guint       prop_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	GdmManager *self;

	self = GDM_MANAGER (object);

	switch (prop_id) {
	case PROP_XDMCP_ENABLED:
		g_value_set_boolean (value, self->priv->xdmcp_enabled);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static GObject *
gdm_manager_constructor (GType                  type,
			 guint                  n_construct_properties,
			 GObjectConstructParam *construct_properties)
{
        GdmManager      *manager;
        GdmManagerClass *klass;

        klass = GDM_MANAGER_CLASS (g_type_class_peek (GDM_TYPE_MANAGER));

        manager = GDM_MANAGER (G_OBJECT_CLASS (gdm_manager_parent_class)->constructor (type,
										       n_construct_properties,
										       construct_properties));

	if (manager->priv->xdmcp_enabled) {
		manager->priv->xdmcp_manager = gdm_xdmcp_manager_new (manager->priv->display_store);
	}

        return G_OBJECT (manager);
}

static void
gdm_manager_class_init (GdmManagerClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gdm_manager_get_property;
	object_class->set_property = gdm_manager_set_property;
	object_class->constructor = gdm_manager_constructor;
	object_class->finalize = gdm_manager_finalize;

	signals [DISPLAY_ADDED] =
		g_signal_new ("display-added",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmManagerClass, display_added),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);
	signals [DISPLAY_REMOVED] =
		g_signal_new ("display-removed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GdmManagerClass, display_removed),
			      NULL,
			      NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

        g_object_class_install_property (object_class,
                                         PROP_XDMCP_ENABLED,
                                         g_param_spec_boolean ("xdmcp-enabled",
                                                               NULL,
                                                               NULL,
							       TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (klass, sizeof (GdmManagerPrivate));

        dbus_g_object_type_install_info (GDM_TYPE_MANAGER, &dbus_glib_gdm_manager_object_info);
}

static void
gdm_manager_init (GdmManager *manager)
{

	manager->priv = GDM_MANAGER_GET_PRIVATE (manager);

	manager->priv->global_cookie = g_string_new (NULL);

	make_global_cookie (manager);

	manager->priv->display_store = gdm_display_store_new ();
}

static void
gdm_manager_finalize (GObject *object)
{
	GdmManager *manager;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GDM_IS_MANAGER (object));

	manager = GDM_MANAGER (object);

	g_return_if_fail (manager->priv != NULL);

	if (manager->priv->xdmcp_manager != NULL) {
		g_object_unref (manager->priv->xdmcp_manager);
	}

	gdm_display_store_clear (manager->priv->display_store);
	g_object_unref (manager->priv->display_store);

	g_string_free (manager->priv->global_cookie, TRUE);

	G_OBJECT_CLASS (gdm_manager_parent_class)->finalize (object);
}

GdmManager *
gdm_manager_new (void)
{
	if (manager_object != NULL) {
		g_object_ref (manager_object);
	} else {
		gboolean res;

		manager_object = g_object_new (GDM_TYPE_MANAGER, NULL);
		g_object_add_weak_pointer (manager_object,
					   (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
	}

	return GDM_MANAGER (manager_object);
}
