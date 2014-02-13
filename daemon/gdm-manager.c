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
#include <glib/gstdio.h>
#include <glib-object.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#include "gdm-common.h"

#include "gdm-dbus-util.h"
#include "gdm-manager.h"
#include "gdm-manager-glue.h"
#include "gdm-display-store.h"
#include "gdm-display-factory.h"
#include "gdm-local-display-factory.h"
#include "gdm-xdmcp-display-factory.h"

#define GDM_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_MANAGER, GdmManagerPrivate))

#define GDM_DBUS_PATH             "/org/gnome/DisplayManager"
#define GDM_MANAGER_PATH          GDM_DBUS_PATH "/Manager"
#define GDM_MANAGER_DISPLAYS_PATH GDM_DBUS_PATH "/Displays"

struct GdmManagerPrivate
{
        GdmDisplayStore        *display_store;
        GdmLocalDisplayFactory *local_factory;
#ifdef HAVE_LIBXDMCP
        GdmXdmcpDisplayFactory *xdmcp_factory;
#endif
        gboolean                xdmcp_enabled;

        gboolean                started;
        gboolean                wait_for_go;
        gboolean                show_local_greeter;

        GDBusProxy               *bus_proxy;
        GDBusConnection          *connection;
        GDBusObjectManagerServer *object_manager;
};

enum {
        PROP_0,
        PROP_XDMCP_ENABLED,
        PROP_SHOW_LOCAL_GREETER
};

enum {
        DISPLAY_ADDED,
        DISPLAY_REMOVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_manager_class_init  (GdmManagerClass *klass);
static void     gdm_manager_init        (GdmManager      *manager);
static void     gdm_manager_finalize    (GObject         *object);

static gpointer manager_object = NULL;

static void manager_interface_init (GdmDBusManagerIface *interface);

G_DEFINE_TYPE_WITH_CODE (GdmManager,
                         gdm_manager,
                         GDM_DBUS_TYPE_MANAGER_SKELETON,
                         G_IMPLEMENT_INTERFACE (GDM_DBUS_TYPE_MANAGER,
                                                manager_interface_init));

#ifdef WITH_SYSTEMD
static char *
get_session_id_for_pid_systemd (pid_t    pid,
                                GError **error)
{
        char *session, *gsession;
        int ret;

        session = NULL;
        ret = sd_pid_get_session (pid, &session);
        if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting session id from systemd: %s",
                             g_strerror (-ret));
                return NULL;
        }

        if (session != NULL) {
                gsession = g_strdup (session);
                free (session);

                return gsession;
        } else {
                return NULL;
        }
}
#endif

#ifdef WITH_CONSOLE_KIT
static char *
get_session_id_for_pid_consolekit (GDBusConnection  *connection,
                                   pid_t             pid,
                                   GError          **error)
{
        GVariant *reply;
        char *retval;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.ConsoleKit",
                                             "/org/freedesktop/ConsoleKit/Manager",
                                             "org.freedesktop.ConsoleKit.Manager",
                                             "GetSessionForUnixProcess",
                                             g_variant_new ("(u)", pid),
                                             G_VARIANT_TYPE ("(o)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, error);
        if (reply == NULL) {
                return NULL;
        }

        g_variant_get (reply, "(o)", &retval);
        g_variant_unref (reply);

        return retval;
}
#endif

static char *
get_session_id_for_pid (GDBusConnection  *connection,
                        pid_t             pid,
                        GError          **error)
{
#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                return get_session_id_for_pid_systemd (pid, error);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return get_session_id_for_pid_consolekit (connection, pid, error);
#endif

        return NULL;
}

#ifdef WITH_SYSTEMD
static gboolean
get_uid_for_systemd_session_id (const char  *session_id,
                                uid_t       *uid,
                                GError     **error)
{
        int ret;

        ret = sd_session_get_uid (session_id, uid);
        if (ret < 0) {
                g_set_error (error,
                             GDM_DISPLAY_ERROR,
                             GDM_DISPLAY_ERROR_GETTING_SESSION_INFO,
                             "Error getting uid for session id %s from systemd: %s",
                             session_id,
                             g_strerror (-ret));
                return FALSE;
        }

        return TRUE;
}
#endif

#ifdef WITH_CONSOLE_KIT
static gboolean
get_uid_for_consolekit_session_id (GDBusConnection  *connection,
                                   const char       *session_id,
                                   uid_t            *out_uid,
                                   GError          **error)
{
        GVariant *reply;
        guint32 uid;

        reply = g_dbus_connection_call_sync (connection,
                                             "org.freedesktop.ConsoleKit",
                                             session_id,
                                             "org.freedesktop.ConsoleKit.Session",
                                             "GetUnixUser",
                                             NULL,
                                             G_VARIANT_TYPE ("(u)"),
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL,
                                             error);
        if (reply == NULL) {
                return FALSE;
        }

        g_variant_get (reply, "(u)", &uid);
        g_variant_unref (reply);

        *out_uid = (uid_t) uid;

        return TRUE;
}
#endif

static gboolean
get_uid_for_session_id (GDBusConnection  *connection,
                        const char       *session_id,
                        uid_t            *uid,
                        GError          **error)
{
#ifdef WITH_SYSTEMD
        if (LOGIND_RUNNING()) {
                return get_uid_for_systemd_session_id (session_id, uid, error);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return get_uid_for_consolekit_session_id (connection, session_id, uid, error);
#endif

        return FALSE;
}

static gboolean
lookup_by_session_id (const char *id,
                      GdmDisplay *display,
                      gpointer    user_data)
{
        const char *looking_for = user_data;
        char *current;
        gboolean res;

        current = gdm_display_get_session_id (display);

        res = g_strcmp0 (current, looking_for) == 0;

        g_free (current);

        return res;
}

static GdmDisplay *
get_display_and_details_for_bus_sender (GdmManager       *self,
                                        GDBusConnection  *connection,
                                        const char       *sender,
                                        GPid             *out_pid,
                                        uid_t            *out_uid)
{
        GdmDisplay *display = NULL;
        char       *session_id = NULL;
        GError     *error = NULL;
        int         ret;
        GPid        pid;
        uid_t       caller_uid, session_uid;

        ret = gdm_dbus_get_pid_for_name (sender, &pid, &error);

        if (!ret) {
                g_debug ("GdmManager: Error while retrieving pid for sender: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        ret = gdm_dbus_get_uid_for_name (sender, &caller_uid, &error);

        if (!ret) {
                g_debug ("GdmManager: Error while retrieving uid for sender: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        session_id = get_session_id_for_pid (connection, pid, &error);

        if (session_id == NULL) {
                g_debug ("GdmManager: Error while retrieving session id for sender: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (!get_uid_for_session_id (connection, session_id, &session_uid, &error)) {
                g_debug ("GdmManager: Error while retrieving uid for session: %s",
                         error->message);
                g_error_free (error);
                goto out;
        }

        if (caller_uid != session_uid) {
                g_debug ("GdmManager: uid for sender and uid for session don't match");
                goto out;
        }

        display = gdm_display_store_find (self->priv->display_store,
                                          lookup_by_session_id,
                                          (gpointer) session_id);
out:
        g_free (session_id);

        if (display != NULL) {
            if (out_pid != NULL)
                *out_pid = pid;

            if (out_uid != NULL)
                *out_uid = session_uid;
        }
        return display;
}

static gboolean
gdm_manager_handle_open_session (GdmDBusManager        *manager,
                                 GDBusMethodInvocation *invocation)
{
        GdmManager       *self = GDM_MANAGER (manager);
        const char       *sender = NULL;
        GError           *error = NULL;
        GDBusConnection  *connection;
        GdmDisplay       *display;
        char             *address;
        GPid              pid;
        uid_t             uid;

        g_debug ("GdmManager: trying to open new session");

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);
        display = get_display_and_details_for_bus_sender (self, connection, sender, &pid, &uid);

        if (display == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("No session available"));

                return TRUE;
        }

        address = gdm_display_open_session_sync (display, pid, uid, NULL, &error);

        if (address == NULL) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                g_error_free (error);
                return TRUE;
        }

        gdm_dbus_manager_complete_open_session (GDM_DBUS_MANAGER (manager),
                                                invocation,
                                                address);
        g_free (address);

        return TRUE;
}

static gboolean
gdm_manager_handle_open_reauthentication_channel (GdmDBusManager        *manager,
                                                  GDBusMethodInvocation *invocation,
                                                  const char            *username)
{
        GdmManager       *self = GDM_MANAGER (manager);
        const char       *sender = NULL;
        GError           *error = NULL;
        GDBusConnection  *connection;
        GdmDisplay       *display;
        char             *address;
        GPid              pid;
        uid_t             uid;

        g_debug ("GdmManager: trying to open reauthentication channel for user %s", username);

        sender = g_dbus_method_invocation_get_sender (invocation);
        connection = g_dbus_method_invocation_get_connection (invocation);
        display = get_display_and_details_for_bus_sender (self, connection, sender, &pid, &uid);

        if (display == NULL) {
                g_dbus_method_invocation_return_error_literal (invocation,
                                                               G_DBUS_ERROR,
                                                               G_DBUS_ERROR_ACCESS_DENIED,
                                                               _("No session available"));

                return TRUE;
        }

        address = gdm_display_open_reauthentication_channel_sync (display,
                                                                  username,
                                                                  pid,
                                                                  uid,
                                                                  NULL,
                                                                  &error);

        if (address == NULL) {
                g_dbus_method_invocation_return_gerror (invocation, error);
                g_error_free (error);
                return TRUE;
        }

        gdm_dbus_manager_complete_open_reauthentication_channel (GDM_DBUS_MANAGER (manager),
                                                                 invocation,
                                                                 address);
        g_free (address);

        return TRUE;
}

static void
manager_interface_init (GdmDBusManagerIface *interface)
{
        interface->handle_open_session = gdm_manager_handle_open_session;
        interface->handle_open_reauthentication_channel = gdm_manager_handle_open_reauthentication_channel;
}

static void
on_display_removed (GdmDisplayStore *display_store,
                    const char      *id,
                    GdmManager      *manager)
{
        GdmDisplay *display;

        display = gdm_display_store_lookup (display_store, id);

        if (display != NULL) {
                g_dbus_object_manager_server_unexport (manager->priv->object_manager, id);

                g_signal_emit (manager, signals[DISPLAY_REMOVED], 0, id);
        }
}

static void
on_display_added (GdmDisplayStore *display_store,
                  const char      *id,
                  GdmManager      *manager)
{
        GdmDisplay *display;

        display = gdm_display_store_lookup (display_store, id);

        if (display != NULL) {
                g_dbus_object_manager_server_export (manager->priv->object_manager,
                                                     gdm_display_get_object_skeleton (display));
                g_signal_emit (manager, signals[DISPLAY_ADDED], 0, id);
        }
}

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
  /org/gnome/DisplayManager/Displays \
  org.freedesktop.ObjectManager.GetAll
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

void
gdm_manager_stop (GdmManager *manager)
{
        g_debug ("GdmManager: GDM stopping");

        if (manager->priv->local_factory != NULL) {
                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->priv->local_factory));
        }

#ifdef HAVE_LIBXDMCP
        if (manager->priv->xdmcp_factory != NULL) {
                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->priv->xdmcp_factory));
        }
#endif

        manager->priv->started = FALSE;
}

void
gdm_manager_start (GdmManager *manager)
{
        g_debug ("GdmManager: GDM starting to manage displays");

        if (! manager->priv->wait_for_go && (!manager->priv->xdmcp_enabled || manager->priv->show_local_greeter)) {
                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->priv->local_factory));
        }

#ifdef HAVE_LIBXDMCP
        /* Accept remote connections */
        if (manager->priv->xdmcp_enabled && ! manager->priv->wait_for_go) {
                if (manager->priv->xdmcp_factory != NULL) {
                        g_debug ("GdmManager: Accepting XDMCP connections...");
                        gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->priv->xdmcp_factory));
                }
        }
#endif

        manager->priv->started = TRUE;
}

void
gdm_manager_set_wait_for_go (GdmManager *manager,
                             gboolean    wait_for_go)
{
        if (manager->priv->wait_for_go != wait_for_go) {
                manager->priv->wait_for_go = wait_for_go;

                if (! wait_for_go) {
                        /* we got a go */
                        if (!manager->priv->xdmcp_enabled || manager->priv->show_local_greeter) {
                                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->priv->local_factory));
                        }

#ifdef HAVE_LIBXDMCP
                        if (manager->priv->xdmcp_enabled && manager->priv->xdmcp_factory != NULL) {
                                g_debug ("GdmManager: Accepting XDMCP connections...");
                                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->priv->xdmcp_factory));
                        }
#endif
                }
        }
}

static gboolean
register_manager (GdmManager *manager)
{
        GError *error = NULL;
        GDBusObjectManagerServer *object_server;

        error = NULL;
        manager->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
                                                    NULL,
                                                    &error);
        if (manager->priv->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        object_server = g_dbus_object_manager_server_new (GDM_MANAGER_DISPLAYS_PATH);
        g_dbus_object_manager_server_set_connection (object_server, manager->priv->connection);
        manager->priv->object_manager = object_server;

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (manager),
                                               manager->priv->connection,
                                               GDM_MANAGER_PATH,
                                               &error)) {
                g_critical ("error exporting interface to %s: %s",
                            GDM_MANAGER_PATH,
                            error->message);
                g_error_free (error);
                exit (1);
        }

        return TRUE;
}

void
gdm_manager_set_xdmcp_enabled (GdmManager *manager,
                               gboolean    enabled)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        if (manager->priv->xdmcp_enabled != enabled) {
                manager->priv->xdmcp_enabled = enabled;
#ifdef HAVE_LIBXDMCP
                if (manager->priv->xdmcp_enabled) {
                        manager->priv->xdmcp_factory = gdm_xdmcp_display_factory_new (manager->priv->display_store);
                        if (manager->priv->started) {
                                gdm_display_factory_start (GDM_DISPLAY_FACTORY (manager->priv->xdmcp_factory));
                        }
                } else {
                        if (manager->priv->started) {
                                gdm_display_factory_stop (GDM_DISPLAY_FACTORY (manager->priv->xdmcp_factory));
                        }

                        g_object_unref (manager->priv->xdmcp_factory);
                        manager->priv->xdmcp_factory = NULL;
                }
#endif
        }

}

void
gdm_manager_set_show_local_greeter (GdmManager *manager,
                                    gboolean    show_local_greeter)
{
        g_return_if_fail (GDM_IS_MANAGER (manager));

        manager->priv->show_local_greeter = show_local_greeter;
}

static void
gdm_manager_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue  *value,
                          GParamSpec    *pspec)
{
        GdmManager *self;

        self = GDM_MANAGER (object);

        switch (prop_id) {
        case PROP_XDMCP_ENABLED:
                gdm_manager_set_xdmcp_enabled (self, g_value_get_boolean (value));
                break;
        case PROP_SHOW_LOCAL_GREETER:
                gdm_manager_set_show_local_greeter (self, g_value_get_boolean (value));
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
        case PROP_SHOW_LOCAL_GREETER:
                g_value_set_boolean (value, self->priv->show_local_greeter);
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

        manager = GDM_MANAGER (G_OBJECT_CLASS (gdm_manager_parent_class)->constructor (type,
                                                                                       n_construct_properties,
                                                                                       construct_properties));

        gdm_dbus_manager_set_version (GDM_DBUS_MANAGER (manager), PACKAGE_VERSION);

        manager->priv->local_factory = gdm_local_display_factory_new (manager->priv->display_store);

#ifdef HAVE_LIBXDMCP
        if (manager->priv->xdmcp_enabled) {
                manager->priv->xdmcp_factory = gdm_xdmcp_display_factory_new (manager->priv->display_store);
        }
#endif

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
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmManagerPrivate));
}

static void
gdm_manager_init (GdmManager *manager)
{

        manager->priv = GDM_MANAGER_GET_PRIVATE (manager);

        manager->priv->display_store = gdm_display_store_new ();

        g_signal_connect (G_OBJECT (manager->priv->display_store),
                          "display-added",
                          G_CALLBACK (on_display_added),
                          manager);

        g_signal_connect (G_OBJECT (manager->priv->display_store),
                          "display-removed",
                          G_CALLBACK (on_display_removed),
                          manager);
}

static void
unexport_display (const char *id,
                  GdmDisplay *display,
                  GdmManager *manager)
{
        if (!g_dbus_connection_is_closed (manager->priv->connection))
                g_dbus_object_manager_server_unexport (manager->priv->object_manager, id);
}

static void
gdm_manager_finalize (GObject *object)
{
        GdmManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_MANAGER (object));

        manager = GDM_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

#ifdef HAVE_LIBXDMCP
        g_clear_object (&manager->priv->xdmcp_factory);
#endif
        g_clear_object (&manager->priv->local_factory);

        g_signal_handlers_disconnect_by_func (G_OBJECT (manager->priv->display_store),
                                              G_CALLBACK (on_display_added),
                                              manager);
        g_signal_handlers_disconnect_by_func (G_OBJECT (manager->priv->display_store),
                                              G_CALLBACK (on_display_removed),
                                              manager);

        if (!g_dbus_connection_is_closed (manager->priv->connection)) {
                gdm_display_store_foreach (manager->priv->display_store,
                                           (GdmDisplayStoreFunc)unexport_display,
                                           manager);
                g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (manager));
        }

        gdm_display_store_clear (manager->priv->display_store);

        g_dbus_object_manager_server_set_connection (manager->priv->object_manager, NULL);

        g_clear_object (&manager->priv->connection);
        g_clear_object (&manager->priv->object_manager);

        g_object_unref (manager->priv->display_store);

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
