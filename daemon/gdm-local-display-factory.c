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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "gdm-display-factory.h"
#include "gdm-local-display-factory.h"
#include "gdm-local-display-factory-glue.h"

#include "gdm-marshal.h"
#include "gdm-display-store.h"
#include "gdm-static-display.h"
#include "gdm-dynamic-display.h"
#include "gdm-transient-display.h"
#include "gdm-static-factory-display.h"
#include "gdm-product-display.h"

#define GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_LOCAL_DISPLAY_FACTORY, GdmLocalDisplayFactoryPrivate))

#define CK_NAME              "org.freedesktop.ConsoleKit"
#define CK_PATH              "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE         "org.freedesktop.ConsoleKit"
#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"

#define CK_SEAT1_PATH                       "/org/freedesktop/ConsoleKit/Seat1"

#define GDM_DBUS_PATH                       "/org/gnome/DisplayManager"
#define GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH GDM_DBUS_PATH "/LocalDisplayFactory"
#define GDM_MANAGER_DBUS_NAME               "org.gnome.DisplayManager.LocalDisplayFactory"

#define HAL_DBUS_NAME                           "org.freedesktop.Hal"
#define HAL_DBUS_MANAGER_PATH                   "/org/freedesktop/Hal/Manager"
#define HAL_DBUS_MANAGER_INTERFACE              "org.freedesktop.Hal.Manager"
#define HAL_DBUS_DEVICE_INTERFACE               "org.freedesktop.Hal.Device"
#define SEAT_PCI_DEVICE_CLASS                   3

#define MAX_DISPLAY_FAILURES 5

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')

#define GDM_DBUS_TYPE_G_STRING_STRING_HASHTABLE (dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_STRING))

struct GdmLocalDisplayFactoryPrivate
{
        DBusGConnection *connection;
        DBusGProxy      *proxy_hal;
        DBusGProxy      *proxy_ck;
        GHashTable      *displays;
        GHashTable      *displays_by_session;
        GHashTable      *managed_seat_proxies;

        /* FIXME: this needs to be per seat? */
        guint            num_failures;
};

enum {
        PROP_0,
};

static void     gdm_local_display_factory_class_init    (GdmLocalDisplayFactoryClass *klass);
static void     gdm_local_display_factory_init          (GdmLocalDisplayFactory      *factory);
static void     gdm_local_display_factory_finalize      (GObject                     *object);

static gboolean create_static_displays                  (GdmLocalDisplayFactory      *factory);
static void     on_display_status_changed               (GdmDisplay                  *display,
                                                         GParamSpec                  *arg1,
                                                         GdmLocalDisplayFactory      *factory);
static void     on_block_console_session_requests_changed (GdmDisplay                  *display,
                                                         GParamSpec                  *arg1,
                                                         GdmLocalDisplayFactory      *factory);

static gpointer local_display_factory_object = NULL;

G_DEFINE_TYPE (GdmLocalDisplayFactory, gdm_local_display_factory, GDM_TYPE_DISPLAY_FACTORY)

GQuark
gdm_local_display_factory_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_local_display_factory_error");
        }

        return ret;
}

static void
listify_hash (gpointer    key,
              GdmDisplay *display,
              GList     **list)
{
        *list = g_list_prepend (*list, key);
}

static int
sort_nums (gpointer a,
           gpointer b)
{
        guint32 num_a;
        guint32 num_b;

        num_a = GPOINTER_TO_UINT (a);
        num_b = GPOINTER_TO_UINT (b);

        if (num_a > num_b) {
                return 1;
        } else if (num_a < num_b) {
                return -1;
        } else {
                return 0;
        }
}

static guint32
take_next_display_number (GdmLocalDisplayFactory *factory)
{
        GList  *list;
        GList  *l;
        guint32 ret;

        ret = 0;
        list = NULL;

        g_hash_table_foreach (factory->priv->displays, (GHFunc)listify_hash, &list);
        if (list == NULL) {
                goto out;
        }

        /* sort low to high */
        list = g_list_sort (list, (GCompareFunc)sort_nums);

        g_debug ("GdmLocalDisplayFactory: Found the following X displays:");
        for (l = list; l != NULL; l = l->next) {
                g_debug ("GdmLocalDisplayFactory: %u", GPOINTER_TO_UINT (l->data));
        }

        for (l = list; l != NULL; l = l->next) {
                guint32 num;
                num = GPOINTER_TO_UINT (l->data);

                /* always fill zero */
                if (l->prev == NULL && num != 0) {
                        ret = 0;
                        break;
                }
                /* now find the first hole */
                if (l->next == NULL || GPOINTER_TO_UINT (l->next->data) != (num + 1)) {
                        ret = num + 1;
                        break;
                }
        }
 out:

        /* now reserve this number */
        g_debug ("GdmLocalDisplayFactory: Reserving X display: %u", ret);
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (ret), NULL);

        return ret;
}

static void
on_display_disposed (GdmLocalDisplayFactory *factory,
                     GdmDisplay             *display)
{
        g_debug ("GdmLocalDisplayFactory: Display %p disposed", display);
}

static void
store_display (GdmLocalDisplayFactory *factory,
               guint32                 num,
               GdmDisplay             *display)
{
        GdmDisplayStore *store;

        g_object_weak_ref (G_OBJECT (display), (GWeakNotify)on_display_disposed, factory);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_add (store, display);

        /* now fill our reserved spot */
        g_hash_table_insert (factory->priv->displays, GUINT_TO_POINTER (num), NULL);
}

static void
store_remove_display (GdmLocalDisplayFactory *factory,
                      guint32                 num,
                      GdmDisplay             *display)
{
        GdmDisplayStore *store;

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));
        gdm_display_store_remove (store, display);

        /* remove from our reserved spot */
        g_hash_table_remove (factory->priv->displays, GUINT_TO_POINTER (num));
}

/*
  Example:
  dbus-send --system --dest=org.gnome.DisplayManager \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/gnome/DisplayManager/Manager \
  org.gnome.DisplayManager.Manager.GetDisplays
*/
gboolean
gdm_local_display_factory_create_transient_display (GdmLocalDisplayFactory *factory,
                                                    char                  **id,
                                                    GError                **error)
{
        gboolean         ret;
        GdmDisplay      *display;
        guint32          num;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        num = take_next_display_number (factory);

        g_debug ("GdmLocalDisplayFactory: Creating transient display %d", num);

        display = gdm_transient_display_new (num);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        store_display (factory, num, display);

        if (! gdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! gdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
}

gboolean
gdm_local_display_factory_create_product_display (GdmLocalDisplayFactory *factory,
                                                  const char             *parent_display_id,
                                                  const char             *relay_address,
                                                  char                  **id,
                                                  GError                **error)
{
        gboolean    ret;
        GdmDisplay *display;
        guint32     num;

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = FALSE;

        g_debug ("GdmLocalDisplayFactory: Creating product display parent %s address:%s",
                 parent_display_id, relay_address);

        num = take_next_display_number (factory);

        g_debug ("GdmLocalDisplayFactory: got display num %u", num);

        display = gdm_product_display_new (num, relay_address);

        /* FIXME: don't hardcode seat1? */
        g_object_set (display, "seat-id", CK_SEAT1_PATH, NULL);

        store_display (factory, num, display);

        if (! gdm_display_manage (display)) {
                display = NULL;
                goto out;
        }

        if (! gdm_display_get_id (display, id, NULL)) {
                display = NULL;
                goto out;
        }

        ret = TRUE;
 out:
        /* ref either held by store or not at all */
        g_object_unref (display);

        return ret;
}

static gboolean
display_has_pending_sessions (GdmLocalDisplayFactory *factory,
                              GdmDisplay             *display)
{
        return g_object_get_data (G_OBJECT (display),
                                  "gdm-local-display-factory-console-session-requests") != NULL;
}

static void
manage_next_pending_session_on_display (GdmLocalDisplayFactory *factory,
                                        GdmDisplay             *display)
{
        GList    *pending_sessions;
        GList    *next_session;
        char     *ssid;


        pending_sessions = g_object_get_data (G_OBJECT (display),
                                              "gdm-local-display-factory-console-session-requests");
        next_session = g_list_last (pending_sessions);

        if (next_session == NULL) {
                return;
        }

        ssid = next_session->data;
        pending_sessions = g_list_delete_link (pending_sessions, next_session);
        g_object_set_data (G_OBJECT (display),
                           "gdm-local-display-factory-console-session-requests",
                           pending_sessions);

        g_object_set (display, "session-id", ssid, NULL);
        g_hash_table_insert (factory->priv->displays_by_session, g_strdup (ssid), g_object_ref (display));
        g_free (ssid);

        gdm_display_manage (display);
}

static void
discard_pending_session_on_display (GdmLocalDisplayFactory *factory,
                                    GdmDisplay             *display,
                                    const char             *ssid)
{
        GList    *pending_sessions;
        GList    *node;

        pending_sessions = g_object_get_data (G_OBJECT (display),
                                              "gdm-local-display-factory-console-session-requests");
        node = g_list_last (pending_sessions);

        while (node != NULL) {
                GList  *prev_node;
                char   *node_ssid;

                prev_node = node->prev;
                node_ssid = node->data;

                if (strcmp (node_ssid, ssid) == 0) {
                        pending_sessions = g_list_delete_link (pending_sessions, node);
                        break;
                }

                node = prev_node;
        }

        g_object_set_data (G_OBJECT (display),
                           "gdm-local-display-factory-console-session-requests",
                           pending_sessions);
}

static void
on_block_console_session_requests_changed (GdmDisplay                  *display,
                                           GParamSpec                  *arg1,
                                           GdmLocalDisplayFactory      *factory)
{
        gboolean  display_is_blocked;
        int       status;

        g_object_get (G_OBJECT (display),
                      "status", &status,
                      "block-console-session-requests",
                      &display_is_blocked, NULL);

        if (display_is_blocked) {
                int number;

                gdm_display_get_x11_display_number (display, &number, NULL);
                g_debug ("GdmLocalDisplayFactory: display :%d is blocked", number);
                return;
        }

        if (status == GDM_DISPLAY_UNMANAGED) {
                manage_next_pending_session_on_display (factory, display);
        }
}

static void
on_display_status_changed (GdmDisplay             *display,
                           GParamSpec             *arg1,
                           GdmLocalDisplayFactory *factory)
{
        int              status;
        GdmDisplayStore *store;
        int              num;
        gboolean         display_is_blocked;

        num = -1;
        gdm_display_get_x11_display_number (display, &num, NULL);
        g_assert (num != -1);

        store = gdm_display_factory_get_display_store (GDM_DISPLAY_FACTORY (factory));

        status = gdm_display_get_status (display);

        g_object_get (G_OBJECT (display),
                      "block-console-session-requests",
                      &display_is_blocked, NULL);

        g_debug ("GdmLocalDisplayFactory: static display status changed: %d", status);
        switch (status) {
        case GDM_DISPLAY_FINISHED:
                /* remove the display number from factory->priv->displays
                   so that it may be reused */
                g_hash_table_remove (factory->priv->displays, GUINT_TO_POINTER (num));
                gdm_display_store_remove (store, display);
                /* reset num failures */
                factory->priv->num_failures = 0;
                break;
        case GDM_DISPLAY_FAILED:
                /* leave the display number in factory->priv->displays
                   so that it doesn't get reused */
                gdm_display_store_remove (store, display);
                factory->priv->num_failures++;
                if (factory->priv->num_failures > MAX_DISPLAY_FAILURES) {
                        /* oh shit */
                        g_warning ("GdmLocalDisplayFactory: maximum number of X display failures reached: check X server log for errors");
                        exit (1);
                }
                break;
        case GDM_DISPLAY_UNMANAGED:
                if (display_has_pending_sessions (factory, display) && !display_is_blocked) {
                        store_display (factory, num, display);
                        manage_next_pending_session_on_display (factory, display);
                }
                break;
        case GDM_DISPLAY_PREPARED:
                break;
        case GDM_DISPLAY_MANAGED:
                break;
        default:
                g_assert_not_reached ();
                break;
        }
}

#ifndef HAVE_STRREP
static void
strrep (char* in, char** out, char* old, char* new)
{
        char* temp;
        char* orig = strdup(in);
        char* found = strstr(orig, old);
        if(!found) {
                *out = malloc(strlen(orig) + 1);
                strcpy(*out, orig);
                return;
        }
        
        int idx = found - orig;
        
        *out = realloc(*out, strlen(orig) - strlen(old) + strlen(new) + 1);
        strncpy(*out, orig, idx);
        strcpy(*out + idx, new);
        strcpy(*out + idx + strlen(new), orig + idx + strlen(old));
        

        temp = malloc(idx+strlen(new)+1);
        strncpy(temp,*out,idx+strlen(new)); 
        temp[idx + strlen(new)] = '\0';

        strrep(found + strlen(old), out, old, new);
        temp = realloc(temp, strlen(temp) + strlen(*out) + 1);
        strcat(temp,*out);
        free(*out);
        *out = temp;
}
#endif

static void
seat_open_session_request (DBusGProxy             *seat_proxy,
                           const char             *ssid,
                           const char             *type,
                           GHashTable             *display_variables,
                           const char             *display_type,
                           GHashTable             *parameters,
                           GdmLocalDisplayFactory *factory)
{
        GdmDisplay *display;
        gint        argc;
        gchar     **argv;
        GError     *error;
        char       *comm = NULL;
        const char *sid = NULL;
        gint32      display_number;
        gboolean    is_chooser;
        gboolean    use_auth;
        int         i;
        char       *xserver_command;
        gboolean    display_is_blocked;
        GList      *pending_sessions;

        g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory));

        display_is_blocked = FALSE;

        display = g_hash_table_lookup (factory->priv->displays_by_session, ssid);

        if (display != NULL) {
                g_object_get (G_OBJECT (display),
                              "block-console-session-requests",
                              &display_is_blocked, NULL);
        }

        if (strcmp (display_type, "X11") != 0) {
                g_warning ("Unknown display type '%s' requested", display_type);
                return;
        }

        xserver_command = g_hash_table_lookup (parameters, "Exec");

        if (! g_shell_parse_argv (xserver_command, &argc, &argv, &error)) {
                g_warning ("Could not parse command %s: %s", xserver_command, error->message);
                g_error_free (error);
                return;
        }

        comm = g_strdup (xserver_command);
        for (i = 0; i < argc; i++) {
                /* replase $display in case of not specified */
                if (g_str_equal (argv[i], "$display")) {
                        display_number = take_next_display_number (factory);
                        strrep (comm, &comm, "$display", "");
                        break;
                }

                /* get display_number in case of specified */
                if (g_str_has_prefix (argv[i], ":")) {
                        display_number = atoi (argv[i]+1);
                        strrep (comm, &comm, argv[i], "");
                        break;
                }
        }
        g_strfreev (argv);

        is_chooser = FALSE;
        if (strstr (comm, "-indirect")) {
                is_chooser = TRUE;
        }

        use_auth = FALSE;
        if (strstr (comm, "-auth $auth")) {
                use_auth = TRUE;
                strrep (comm, &comm, "-auth $auth", "");
        }

        if (strstr (comm, "$vt")) {
                use_auth = TRUE;
                strrep (comm, &comm, "$vt", "");
        }

        if (display == NULL) {
                if (is_chooser) {
                        /* TODO: Start a xdmcp chooser as request */

                        /* display = gdm_xdmcp_chooser_display_new (display_number); */
                } else {
                        display = gdm_dynamic_display_new (display_number);
                }

                if (display == NULL) {
                        g_warning ("Unable to create display: %d", display_number);
                        g_free (comm);
                        return;
                }

                g_object_set (display, "session-id", ssid, NULL);
                g_hash_table_insert (factory->priv->displays_by_session, g_strdup (ssid), g_object_ref (display));

                sid = dbus_g_proxy_get_path (seat_proxy);
                if (IS_STR_SET (sid))
                        g_object_set (display, "seat-id", sid, NULL);
                if (IS_STR_SET (comm))
                        g_object_set (display, "x11-command", comm, NULL);
                g_free (comm);
                g_object_set (display, "use-auth", use_auth, NULL);

                g_signal_connect (display,
                                  "notify::status",
                                  G_CALLBACK (on_display_status_changed),
                                  factory);

                g_signal_connect (display,
                                  "notify::block-console-session-requests",
                                  G_CALLBACK (on_block_console_session_requests_changed),
                                  factory);

                store_display (factory, display_number, display);

                g_object_unref (display);

                if (! gdm_display_manage (display)) {
                    gdm_display_unmanage (display);
                }

                return;
        }

        /* FIXME: Make sure the display returned is compatible
         */

        if (!display_is_blocked) {
                /* FIXME: What do we do here?
                 */
                g_warning ("Got console request to add display for session that "
                           "already has a display, and display is already in "
                           "use");
                return;
        }

        pending_sessions = g_object_get_data (G_OBJECT (display),
                                              "gdm-local-display-factory-console-session-requests");
        pending_sessions = g_list_prepend (pending_sessions, g_strdup (ssid));

        g_object_set_data (G_OBJECT (display),
                           "gdm-local-display-factory-console-session-requests",
                           pending_sessions);
}

static void
seat_close_session_request (DBusGProxy             *seat_proxy,
                            const char             *ssid,
                            GdmLocalDisplayFactory *factory)
{
        GdmDisplay      *display;
        int              display_number;
        char            *display_ssid;

        g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory));

        display = g_hash_table_lookup (factory->priv->displays_by_session, ssid);

        if (display == NULL) {
                g_debug ("GdmLocalDisplayFactory: display for session '%s' doesn't exists", ssid);
                return;
        }

        g_object_get (G_OBJECT (display), "session-id", &display_ssid, NULL);

        if (display_ssid == NULL || strcmp (ssid, display_ssid) != 0) {
                g_free (display_ssid);
                discard_pending_session_on_display (factory, display, ssid);
                return;
        }
        g_free (display_ssid);
        g_hash_table_remove (factory->priv->displays_by_session, ssid);

        if (! gdm_display_unmanage (display)) {
                display = NULL;
                return;
        }

        gdm_display_get_x11_display_number (display, &display_number, NULL);
        store_remove_display (factory, display_number, display);
}

static void
seat_remove_request (DBusGProxy             *seat_proxy,
                     GdmLocalDisplayFactory *factory)
{
        GHashTableIter iter;
        gpointer key, value;
        const char *sid_to_remove;
        GQueue      ssids_to_remove;

        sid_to_remove = dbus_g_proxy_get_path (seat_proxy);

        g_queue_init (&ssids_to_remove);
        g_hash_table_iter_init (&iter, factory->priv->displays_by_session);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmDisplay *display;
                char       *sid;

                display = value;

                gdm_display_get_seat_id (display, &sid, NULL);

                if (strcmp (sid, sid_to_remove) == 0) {
                        char       *ssid;

                        gdm_display_get_session_id (display, &ssid, NULL);

                        g_queue_push_tail (&ssids_to_remove, ssid);
                }

                g_free (sid);
        }

        while (!g_queue_is_empty (&ssids_to_remove)) {
                char       *ssid;

                ssid = g_queue_pop_head (&ssids_to_remove);

                seat_close_session_request (seat_proxy, ssid, factory);

                g_free (ssid);
        }

        g_hash_table_remove (factory->priv->managed_seat_proxies, sid_to_remove);

        dbus_g_proxy_call_no_reply (seat_proxy,
                                    "Unmanage",
                                    DBUS_TYPE_G_OBJECT_PATH, sid_to_remove,
                                    G_TYPE_INVALID,
                                    G_TYPE_INVALID);

        dbus_g_proxy_call_no_reply (seat_proxy,
                                    "RemoveSeat",
                                    DBUS_TYPE_G_OBJECT_PATH, sid_to_remove,
                                    G_TYPE_INVALID,
                                    G_TYPE_INVALID);
}

static void
manage_static_sessions_per_seat (GdmLocalDisplayFactory *factory,
                                 const char             *sid)
{
        DBusGProxy *proxy;

        proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                           CK_NAME,
                                           sid,
                                           CK_SEAT_INTERFACE);

        if (proxy == NULL) {
                g_warning ("Failed to connect to the ConsoleKit seat object");
                return;
        }

        dbus_g_object_register_marshaller (gdm_marshal_VOID__STRING_STRING_POINTER_STRING_POINTER,
                                           G_TYPE_NONE,
                                           DBUS_TYPE_G_OBJECT_PATH, G_TYPE_STRING,
                                           GDM_DBUS_TYPE_G_STRING_STRING_HASHTABLE,
                                           G_TYPE_STRING,
                                           GDM_DBUS_TYPE_G_STRING_STRING_HASHTABLE,
                                           G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "OpenSessionRequest",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_STRING,
                                 GDM_DBUS_TYPE_G_STRING_STRING_HASHTABLE,
                                 G_TYPE_STRING,
                                 GDM_DBUS_TYPE_G_STRING_STRING_HASHTABLE,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "CloseSessionRequest",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_INVALID);
        dbus_g_proxy_add_signal (proxy,
                                 "RemoveRequest",
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (proxy,
                                     "OpenSessionRequest",
                                     G_CALLBACK (seat_open_session_request),
                                     factory,
                                     NULL);
        dbus_g_proxy_connect_signal (proxy,
                                     "CloseSessionRequest",
                                     G_CALLBACK (seat_close_session_request),
                                     factory,
                                     NULL);

        dbus_g_proxy_connect_signal (proxy,
                                     "RemoveRequest",
                                     G_CALLBACK (seat_remove_request),
                                     factory,
                                     NULL);

        dbus_g_proxy_call_no_reply (proxy,
                                    "Manage",
                                    G_TYPE_INVALID,
                                    G_TYPE_INVALID);

        g_hash_table_insert (factory->priv->managed_seat_proxies,
                             g_strdup (dbus_g_proxy_get_path (proxy)),
                             proxy);
}

static void
seat_added (DBusGProxy             *mgr_proxy,
            const char             *sid,
            const char             *type,
            GdmLocalDisplayFactory *factory)
{
        if (strcmp (type, "Default") == 0) {
                manage_static_sessions_per_seat (factory, sid);
        }
}

static gboolean
create_static_displays (GdmLocalDisplayFactory *factory)
{
        GError     *error;
        gboolean    res;
        GPtrArray  *seats;
        int         i;

        seats = NULL;

        error = NULL;
        res = dbus_g_proxy_call (factory->priv->proxy_ck,
                                 "GetUnmanagedSeats",
                                 &error,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                 &seats,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Failed to get list of unmanaged seats: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        for (i = 0; i < seats->len; i++) {
                char *sid;

                sid = g_ptr_array_index (seats, i);

                manage_static_sessions_per_seat (factory, sid);

                g_free (sid);
        }

        return TRUE;

}

#if 0
static void
create_display_for_device (GdmLocalDisplayFactory *factory,
                           DBusGProxy             *device_proxy)
{
        create_display (factory);
}

static void
create_displays_for_pci_devices (GdmLocalDisplayFactory *factory)
{
        char      **devices;
        const char *key;
        const char *value;
        GError     *error;
        gboolean    res;
        int         i;

        g_debug ("GdmLocalDisplayFactory: Getting PCI seat devices");

        key = "info.bus";
        value = "pci";

        devices = NULL;
        error = NULL;
        res = dbus_g_proxy_call (factory->priv->proxy,
                                 "FindDeviceStringMatch",
                                 &error,
                                 G_TYPE_STRING, key,
                                 G_TYPE_STRING, value,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRV, &devices,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Unable to query HAL: %s", error->message);
                g_error_free (error);
        }

        /* now look for pci class 3 */
        key = "pci.device_class";
        for (i = 0; devices [i] != NULL; i++) {
                DBusGProxy *device_proxy;
                int         class_val;

                device_proxy = dbus_g_proxy_new_for_name (factory->priv->connection,
                                                          HAL_DBUS_NAME,
                                                          devices [i],
                                                          HAL_DBUS_DEVICE_INTERFACE);
                if (device_proxy == NULL) {
                        continue;
                }

                error = NULL;
                res = dbus_g_proxy_call (device_proxy,
                                         "GetPropertyInteger",
                                         &error,
                                         G_TYPE_STRING, key,
                                         G_TYPE_INVALID,
                                         G_TYPE_INT, &class_val,
                                         G_TYPE_INVALID);
                if (! res) {
                        g_warning ("Unable to query HAL: %s", error->message);
                        g_error_free (error);
                }

                if (class_val == SEAT_PCI_DEVICE_CLASS) {
                        g_debug ("GdmLocalDisplayFactory: Found device: %s", devices [i]);
                        create_display_for_device (factory, device_proxy);
                }

                g_object_unref (device_proxy);
        }

        g_strfreev (devices);
}
#endif

static gboolean
gdm_local_display_factory_start (GdmDisplayFactory *base_factory)
{
        gboolean                ret;
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        ret = TRUE;

        /* FIXME: use seat configuration */
#if 0
        create_displays_for_pci_devices (factory);
#else
        create_static_displays (factory);
#endif

        return ret;
}

static gboolean
gdm_local_display_factory_stop (GdmDisplayFactory *base_factory)
{
        GdmLocalDisplayFactory *factory = GDM_LOCAL_DISPLAY_FACTORY (base_factory);

        g_return_val_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (factory), FALSE);

        return TRUE;
}

static void
gdm_local_display_factory_set_property (GObject       *object,
                                        guint          prop_id,
                                        const GValue  *value,
                                        GParamSpec    *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_local_display_factory_get_property (GObject    *object,
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

static gboolean
register_factory (GdmLocalDisplayFactory *factory)
{
        GError *error = NULL;

        error = NULL;
        factory->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (factory->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        dbus_g_connection_register_g_object (factory->priv->connection, GDM_LOCAL_DISPLAY_FACTORY_DBUS_PATH, G_OBJECT (factory));

        return TRUE;
}

static gboolean
connect_to_hal (GdmLocalDisplayFactory *factory)
{
        factory->priv->proxy_hal = dbus_g_proxy_new_for_name (factory->priv->connection,
                                                              HAL_DBUS_NAME,
                                                              HAL_DBUS_MANAGER_PATH,
                                                              HAL_DBUS_MANAGER_INTERFACE);
        if (factory->priv->proxy_hal == NULL) {
                g_warning ("Couldn't create proxy for HAL Manager");
                return FALSE;
        }

        return TRUE;
}

static void
disconnect_from_hal (GdmLocalDisplayFactory *factory)
{
        if (factory->priv->proxy_hal == NULL) {
                g_object_unref (factory->priv->proxy_hal);
        }
}

static gboolean
connect_to_ck (GdmLocalDisplayFactory *factory)
{
        GdmLocalDisplayFactoryPrivate *priv;

        priv = factory->priv;

        priv->proxy_ck = dbus_g_proxy_new_for_name (priv->connection,
                                                    CK_NAME,
                                                    CK_MANAGER_PATH,
                                                    CK_MANAGER_INTERFACE);

        if (priv->proxy_ck == NULL) {
                g_warning ("Couldn't create proxy for ConsoleKit Manager");
                return FALSE;
        }

        dbus_g_proxy_add_signal (priv->proxy_ck,
                                 "SeatAdded",
                                 DBUS_TYPE_G_OBJECT_PATH,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (priv->proxy_ck,
                                     "SeatAdded",
                                     G_CALLBACK (seat_added),
                                     factory,
                                     NULL);
}

static void
disconnect_from_ck (GdmLocalDisplayFactory *factory)
{
        if (factory->priv->proxy_hal == NULL) {
                g_object_unref (factory->priv->proxy_hal);
        }
}

static GObject *
gdm_local_display_factory_constructor (GType                  type,
                                       guint                  n_construct_properties,
                                       GObjectConstructParam *construct_properties)
{
        GdmLocalDisplayFactory      *factory;
        gboolean                     res;

        factory = GDM_LOCAL_DISPLAY_FACTORY (G_OBJECT_CLASS (gdm_local_display_factory_parent_class)->constructor (type,
                                                                                                                   n_construct_properties,
                                                                                                                   construct_properties));

        res = register_factory (factory);
        if (! res) {
                g_warning ("Unable to register local display factory with system bus");
        }

        connect_to_hal (factory);
        connect_to_ck (factory);

        return G_OBJECT (factory);
}

static void
gdm_local_display_factory_class_init (GdmLocalDisplayFactoryClass *klass)
{
        GObjectClass           *object_class = G_OBJECT_CLASS (klass);
        GdmDisplayFactoryClass *factory_class = GDM_DISPLAY_FACTORY_CLASS (klass);

        object_class->get_property = gdm_local_display_factory_get_property;
        object_class->set_property = gdm_local_display_factory_set_property;
        object_class->finalize = gdm_local_display_factory_finalize;
        object_class->constructor = gdm_local_display_factory_constructor;

        factory_class->start = gdm_local_display_factory_start;
        factory_class->stop = gdm_local_display_factory_stop;

        g_type_class_add_private (klass, sizeof (GdmLocalDisplayFactoryPrivate));

        dbus_g_object_type_install_info (GDM_TYPE_LOCAL_DISPLAY_FACTORY, &dbus_glib_gdm_local_display_factory_object_info);
}

static void
gdm_local_display_factory_init (GdmLocalDisplayFactory *factory)
{
        factory->priv = GDM_LOCAL_DISPLAY_FACTORY_GET_PRIVATE (factory);

        factory->priv->displays = g_hash_table_new (NULL, NULL);
        factory->priv->displays_by_session = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                    (GDestroyNotify) g_free,
                                                                    (GDestroyNotify) g_object_unref);

        factory->priv->managed_seat_proxies = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                    (GDestroyNotify) g_free,
                                                                    (GDestroyNotify) g_object_unref);
}

static void
gdm_local_display_factory_finalize (GObject *object)
{
        GdmLocalDisplayFactory *factory;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_LOCAL_DISPLAY_FACTORY (object));

        factory = GDM_LOCAL_DISPLAY_FACTORY (object);

        g_return_if_fail (factory->priv != NULL);

        g_hash_table_destroy (factory->priv->displays);
        g_hash_table_destroy (factory->priv->displays_by_session);
        g_hash_table_destroy (factory->priv->managed_seat_proxies);

        disconnect_from_hal (factory);
        disconnect_from_ck (factory);

        G_OBJECT_CLASS (gdm_local_display_factory_parent_class)->finalize (object);
}

GdmLocalDisplayFactory *
gdm_local_display_factory_new (GdmDisplayStore *store)
{
        if (local_display_factory_object != NULL) {
                g_object_ref (local_display_factory_object);
        } else {
                local_display_factory_object = g_object_new (GDM_TYPE_LOCAL_DISPLAY_FACTORY,
                                                             "display-store", store,
                                                             NULL);
                g_object_add_weak_pointer (local_display_factory_object,
                                           (gpointer *) &local_display_factory_object);
        }

        return GDM_LOCAL_DISPLAY_FACTORY (local_display_factory_object);
}
