/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright Red Hat
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
 */

#include "config.h"

#include <grp.h>
#include <shadow.h>
#include <sys/types.h>
#ifdef HAVE_USERDB
#include <systemd/sd-varlink.h>
#endif

#include "gdm-common.h"
#include "gdm-file-utils.h"
#include "gdm-dynamic-user-store.h"

G_STATIC_ASSERT (sizeof(uid_t) == sizeof(guint));

#define GDM_SERVICE_ID "org.gnome.DisplayManager"
#define GREETER_UID_COUNT ((size_t)(GREETER_UID_MAX - GREETER_UID_MIN) + 1)
#define GID_NOBODY ((gid_t) 65534)
#define UID_INVALID ((uid_t) -1)

#define USERDB_SOCKET_DIR "/run/systemd/userdb"

#ifndef SD_VARLINK_SERVER_MODE_MKDIR_0755
#define SD_VARLINK_SERVER_MODE_MKDIR_0755 0
#endif

struct _GdmDynamicUserStore
{
        GObject parent;

        GThread *worker;
        GCancellable *worker_cancellable;

        GMutex mutex;
        GHashTable *by_name; /* Owns the DynamicUser objects */
        GHashTable *by_uid;  /* Just an index to look up quickly by UID */

        uid_t next_alloc;
};

typedef struct
{
        char *username;
        char *display_name;
        char *home;
        char *group;
        uid_t uid;
        gid_t gid;
} DynamicUser;

typedef struct
{
        GWeakRef  store;
        gint      cancel_fd;
} WorkerContext;

typedef struct
{
        uid_t uid;
        const char *username;
        const char *service;
} GetUserRecordParams;

typedef struct
{
        const char *username;
        const char *groupname;
        const char *service;
} GetMembershipsParams;

typedef enum
{
  VARLINK_REPLY_FINAL,
  VARLINK_REPLY_MORE_FOLLOW,
} VarlinkReplyMode;

static void     gdm_dynamic_user_store_class_init       (GdmDynamicUserStoreClass *klass);
static void     gdm_dynamic_user_store_init             (GdmDynamicUserStore *store);
static gpointer gdm_dynamic_user_varlink_worker         (gpointer data);
static void     gdm_dynamic_user_store_dispose          (GObject *object);
static void     gdm_dynamic_user_store_finalize         (GObject *object);

G_DEFINE_TYPE (GdmDynamicUserStore, gdm_dynamic_user_store, G_TYPE_OBJECT)

static DynamicUser *
dynamic_user_new (const char   *username,
                  const char   *display_name,
                  const char   *home,
                  uid_t         uid,
                  struct group *group)
{
        DynamicUser *user;

        user = g_new (DynamicUser, 1);
        user->username = g_strdup (username);
        user->display_name = g_strdup (display_name);
        user->home = g_strdup (home);
        user->uid = uid;
        user->group = g_strdup (group->gr_name);
        user->gid = group->gr_gid;

        return user;
}

static void
dynamic_user_free (DynamicUser *user)
{
        g_autoptr (GError) error = NULL;
        g_autoptr (GFile) home = NULL;

        g_debug ("GdmDynUserStore: Deallocating user '%s' (uid: %d)",
                 user->username, user->uid);

        /* Sanity checks, let's not nuke the system by accident */
        if (g_strcmp0 (user->home, "/") == 0 ||
            g_str_has_prefix (user->home, "/home")) {
                g_error ("GdmDynUserStore: Dynamic user home '%s' is in /home or is root! Aborting.", user->home);
        }

        home = g_file_new_for_path (user->home);
        if (!gdm_rm_recursively (home, &error))
                g_warning ("Failed to delete '%s', continuing: %s",
                           user->home, error->message);

        g_free (user->username);
        g_free (user->display_name);
        g_free (user->home);
        g_free (user->group);
        g_free (user);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DynamicUser, dynamic_user_free)

static void
worker_context_free (WorkerContext *ctx)
{
        g_weak_ref_clear (&ctx->store);
        g_close (ctx->cancel_fd, NULL);
        g_free (ctx);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (WorkerContext, worker_context_free)

typedef gboolean PwdLock;

static PwdLock
lock_pwd_db ()
{
        PwdLock lock = lckpwdf () >= 0;
        if (!lock)
                g_warning ("Failed to lock passwd database, ignoring: %m");
        return lock;
}

static void
unlock_pwd_db (PwdLock lock)
{
        if (!lock)
                return;

        if (ulckpwdf () < 0)
                g_warning ("Failed to unlock passwd database, ignoring: %m");
}

G_DEFINE_AUTO_CLEANUP_FREE_FUNC (PwdLock, unlock_pwd_db, FALSE)

#ifdef HAVE_USERDB
G_DEFINE_AUTOPTR_CLEANUP_FUNC (sd_varlink_server, sd_varlink_server_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (sd_event, sd_event_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (sd_json_variant, sd_json_variant_unref)
#endif

GQuark
gdm_dynamic_user_store_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm_dynamic_user_store_error");
        }
        return ret;
}

#ifdef HAVE_USERDB

static gboolean
pick_uid (GdmDynamicUserStore  *store,
          const char           *username,
          uid_t                *ret_uid,
          GError              **error)
{
        gboolean already_used;
        uid_t start;

        start = store->next_alloc;

        do {
                already_used = g_hash_table_contains (store->by_uid, &store->next_alloc) ||
                               gdm_get_pwent_for_uid (store->next_alloc, NULL);

                if (!already_used)
                        *ret_uid = store->next_alloc;

                if (store->next_alloc < GREETER_UID_MAX)
                        store->next_alloc += 1;
                else
                        store->next_alloc = GREETER_UID_MIN;

                if (!already_used)
                        return TRUE;

        } while (store->next_alloc != start);

        g_set_error (error,
                     GDM_DYNAMIC_USER_STORE_ERROR,
                     GDM_DYNAMIC_USER_STORE_ERROR_NO_FREE_UID,
                     "No free UID found for dynamic user");
        return FALSE;
}

#else /* HAVE_USERDB */

static gboolean
pick_uid (GdmDynamicUserStore  *store,
          const char           *username,
          uid_t                *ret_uid,
          GError              **error)
{
        struct passwd *pwe;

        if (gdm_get_pwent_for_name (username, &pwe)) {
                *ret_uid = pwe->pw_uid;
                return TRUE;
        }

        g_set_error (error,
             GDM_DYNAMIC_USER_STORE_ERROR,
             GDM_DYNAMIC_USER_STORE_ERROR_NO_FREE_UID,
             "User '%s' not preallocated and system lacks userdb",
             username);

        return FALSE;
}

#endif /* HAVE_USERDB */

static char *
pick_username (GdmDynamicUserStore *store,
               const char          *preferred)
{
        if (!g_hash_table_contains (store->by_name, preferred))
                return g_strdup (preferred);

        for (size_t i = 2; i <= GREETER_UID_COUNT; i++) {
                g_autofree char *username = NULL;

                username = g_strdup_printf ("%s-%lu", preferred, i);

                if (!g_hash_table_contains (store->by_name, username))
                        return g_steal_pointer (&username);
        }

        /* Reaching this should be impossible, because it means that we've
         * already allocated more than GREETER_UID_COUNT users, which would
         * be caught in pick_uid */
        g_assert_not_reached ();
}

gboolean
gdm_dynamic_user_store_create (GdmDynamicUserStore  *store,
                               const char           *preferred_username,
                               const char           *display_name,
                               const char           *member_of,
                               char                **ret_username,
                               uid_t                *ret_uid,
                               char                **ret_home,
                               GError              **error)
{
        g_autofree char *username = NULL;
        g_auto (PwdLock) pwd_lock = FALSE;
        uid_t uid;
        struct group *grp;
        g_autofree char *home = NULL;
        DynamicUser *user;

        username = pick_username (store, preferred_username);

        g_debug ("GdmDynUserStore: Allocating dynamic user %s (%s)",
                 username, display_name);

        /* We take a system-wide lock on the user database here to eliminate
         * race conditions when checking for used UIDs. Otherwise: someone might
         * claim a UID between us deciding that it's available and us starting
         * to advertise it over userdb. */
        pwd_lock = lock_pwd_db ();

        if (!pick_uid (store, username, &uid, error))
                return FALSE;

        if (!gdm_get_grent_for_name (member_of, &grp)) {
                g_set_error (error,
                             GDM_DYNAMIC_USER_STORE_ERROR,
                             GDM_DYNAMIC_USER_STORE_ERROR_NO_SUCH_GROUP,
                             "Group '%s' doesn't exist",
                             member_of);
                return FALSE;
        }

        home = g_build_filename (GDM_DYN_HOME_DIR, username, NULL);
        if (!gdm_ensure_dir (home, uid, GID_NOBODY, 0700, FALSE, error))
                return FALSE;

        user = dynamic_user_new (username, display_name, home, uid, grp);

        g_mutex_lock (&store->mutex);
        g_hash_table_insert (store->by_name, user->username, user);
        g_hash_table_insert (store->by_uid, &user->uid, user);
        g_mutex_unlock (&store->mutex);

        g_debug ("GdmDynUserStore: Allocated dynamic user '%s' (uid: %d, home: %s)",
                 user->username, uid, home);

        *ret_username = g_steal_pointer (&username);
        *ret_uid = uid;
        *ret_home = g_steal_pointer (&home);
        return TRUE;
}

void
gdm_dynamic_user_store_remove (GdmDynamicUserStore *store,
                               uid_t                uid)
{
        DynamicUser *user;

        user = g_hash_table_lookup (store->by_uid, &uid);
        if (user == NULL)
                return;

        g_mutex_lock (&store->mutex);
        g_hash_table_remove (store->by_uid, &uid);
        g_hash_table_remove (store->by_name, user->username);
        g_mutex_unlock (&store->mutex);
}

static void
gdm_dynamic_user_store_class_init (GdmDynamicUserStoreClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);
        object_class->dispose = gdm_dynamic_user_store_dispose;
        object_class->finalize = gdm_dynamic_user_store_finalize;
}

static void
gdm_dynamic_user_store_init (GdmDynamicUserStore *store)
{
        WorkerContext *worker_ctx = NULL;

        g_mutex_init (&store->mutex);

        store->by_name = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                                (GDestroyNotify) dynamic_user_free);

        store->by_uid = g_hash_table_new (g_int_hash, g_int_equal);

        store->next_alloc = GREETER_UID_MIN;

        worker_ctx = g_new0 (WorkerContext, 1);
        g_weak_ref_init (&worker_ctx->store, store);
        store->worker_cancellable = g_cancellable_new ();
        worker_ctx->cancel_fd = g_cancellable_get_fd (store->worker_cancellable);
        store->worker = g_thread_new ("GDM userdb worker",
                                      gdm_dynamic_user_varlink_worker,
                                      worker_ctx);
}

static void
gdm_dynamic_user_store_dispose (GObject *object)
{
        GdmDynamicUserStore *store;
        g_return_if_fail (GDM_IS_DYNAMIC_USER_STORE (object));
        store = GDM_DYNAMIC_USER_STORE (object);

        g_cancellable_cancel (store->worker_cancellable);
        if (store->worker != NULL)
                g_clear_pointer (&store->worker, g_thread_join);
        g_clear_object (&store->worker_cancellable);

        G_OBJECT_CLASS (gdm_dynamic_user_store_parent_class)->dispose (object);
}


static void
gdm_dynamic_user_store_finalize (GObject *object)
{
        GdmDynamicUserStore *store;
        g_return_if_fail (GDM_IS_DYNAMIC_USER_STORE (object));
        store = GDM_DYNAMIC_USER_STORE (object);

        g_mutex_clear (&store->mutex);
        g_hash_table_destroy (store->by_uid);
        g_hash_table_destroy (store->by_name);

        G_OBJECT_CLASS (gdm_dynamic_user_store_parent_class)->finalize (object);
}

GdmDynamicUserStore *
gdm_dynamic_user_store_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_DYNAMIC_USER_STORE,
                               NULL);

        return GDM_DYNAMIC_USER_STORE (object);
}

#ifdef HAVE_USERDB

static int
dynamic_user_build_record (DynamicUser      *user,
                           sd_json_variant **ret)
{
        return sd_json_buildo (ret, SD_JSON_BUILD_PAIR_OBJECT ("record",
                SD_JSON_BUILD_PAIR_STRING ("userName", user->username),
                SD_JSON_BUILD_PAIR_UNSIGNED ("uid", user->uid),
                SD_JSON_BUILD_PAIR_UNSIGNED ("gid", user->gid),
                SD_JSON_BUILD_PAIR_STRING ("realName", user->display_name),
                SD_JSON_BUILD_PAIR_STRING ("homeDirectory", user->home),
                SD_JSON_BUILD_PAIR_STRING ("shell", NOLOGIN_PATH),
                SD_JSON_BUILD_PAIR_STRING ("service", GDM_SERVICE_ID),
                SD_JSON_BUILD_PAIR_STRING ("disposition", "dynamic")));
}

static int
vl_get_user_record (sd_varlink                *call,
                    sd_json_variant           *json,
                    sd_varlink_method_flags_t  flags,
                    void                      *userdata)
{
        static const sd_json_dispatch_field dispatch[] = {
                { "uid",      SD_JSON_VARIANT_UNSIGNED, sd_json_dispatch_uid_gid,      offsetof (GetUserRecordParams, uid),      0 },
                { "userName", SD_JSON_VARIANT_STRING,   sd_json_dispatch_const_string, offsetof (GetUserRecordParams, username), 0 },
                { "service",  SD_JSON_VARIANT_STRING,   sd_json_dispatch_const_string, offsetof (GetUserRecordParams, service),  0 },
                {}
        };

        GWeakRef *weak_store = userdata;
        g_autoptr (GdmDynamicUserStore) store = NULL;
        GetUserRecordParams params = {
                .uid = UID_INVALID,
        };
        g_autoptr (sd_json_variant) ret = NULL;
        DynamicUser *found = NULL;
        int r;

        r = sd_varlink_dispatch (call, json, dispatch, &params);
        if (r < 0)
                return r;

        if (g_strcmp0 (params.service, GDM_SERVICE_ID) != 0)
                return sd_varlink_error (call,
                                         "io.systemd.UserDatabase.BadService",
                                         NULL);

        store = GDM_DYNAMIC_USER_STORE (g_weak_ref_get (weak_store));
        if (store == NULL)
                return -ECANCELED;

        G_MUTEX_AUTO_LOCK (&store->mutex, locker);

        if (params.uid != UID_INVALID)
                found = g_hash_table_lookup (store->by_uid, &params.uid);
        else if (params.username != NULL)
                found = g_hash_table_lookup (store->by_name, params.username);
        else {
                GHashTableIter iter;
                gpointer value;

                g_hash_table_iter_init (&iter, store->by_name);

                while (g_hash_table_iter_next (&iter, NULL, &value)) {
                        if (found != NULL) {
                                g_autoptr (sd_json_variant) notify = NULL;

                                r = dynamic_user_build_record (found, &notify);
                                if (r < 0)
                                        return r;

                                /* When listing out our dynamic users, we stream
                                 * all but the last with more=true */
                                r = sd_varlink_notify (call, notify);
                                if (r < 0)
                                        return r;
                        }

                        found = value;
                }
        }

        if (found == NULL)
                return sd_varlink_error (call,
                                         "io.systemd.UserDatabase.NoRecordFound",
                                         NULL);

        if ((params.uid != UID_INVALID && params.uid != found->uid) ||
            (params.username != NULL && g_strcmp0 (params.username, found->username) != 0))
                return sd_varlink_error (call,
                                         "io.systemd.UserDatabase.ConflictingRecordFound",
                                         NULL);

        r = dynamic_user_build_record (found, &ret);
        if (r < 0)
                return r;

        return sd_varlink_reply (call, ret);
}

static int
vl_get_group_record (sd_varlink                *call,
                     sd_json_variant           *json,
                     sd_varlink_method_flags_t  flags,
                     void                      *userdata)
{
        const char *service = NULL;
        service = sd_json_variant_string (sd_json_variant_by_key (json, "service"));

        if (g_strcmp0 (service, GDM_SERVICE_ID) != 0)
                return sd_varlink_error (call,
                                         "io.systemd.UserDatabase.BadService",
                                         NULL);

        return sd_varlink_error (call,
                                 "io.systemd.UserDatabase.NoRecordFound",
                                 NULL);
}

static int
reply_user_group (sd_varlink       *call,
                  VarlinkReplyMode  reply_mode,
                  DynamicUser      *user)
{
        g_autoptr (sd_json_variant) ret = NULL;
        int r;

        r = sd_json_buildo (&ret,
                            SD_JSON_BUILD_PAIR_STRING ("userName", user->username),
                            SD_JSON_BUILD_PAIR_STRING ("groupName", user->group));
        if (r < 0)
                return r;

        if (reply_mode == VARLINK_REPLY_MORE_FOLLOW)
                return sd_varlink_notify (call, ret);
        else
                return sd_varlink_reply (call, ret);
}

static gboolean
test_group_membership (DynamicUser *user,
                       const char  *group)
{
        if (user == NULL)
                return FALSE;

        if (group != NULL && g_strcmp0 (user->group, group) != 0)
                return FALSE;

        return TRUE;
}

static int
vl_get_memberships (sd_varlink                *call,
                    sd_json_variant           *json,
                    sd_varlink_method_flags_t  flags,
                    void                      *userdata)
{
        static const sd_json_dispatch_field dispatch[] = {
                { "userName",  SD_JSON_VARIANT_STRING, sd_json_dispatch_const_string, offsetof (GetMembershipsParams, username),  0 },
                { "groupName", SD_JSON_VARIANT_STRING, sd_json_dispatch_const_string, offsetof (GetMembershipsParams, groupname), 0 },
                { "service" ,  SD_JSON_VARIANT_STRING, sd_json_dispatch_const_string, offsetof (GetMembershipsParams, service),   0 },
                {}
        };

        GWeakRef *weak_store = userdata;
        g_autoptr (GdmDynamicUserStore) store = NULL;
        GetMembershipsParams params = {};
        DynamicUser *user = NULL;
        int r;

        r = sd_varlink_dispatch (call, json, dispatch, &params);
        if (r < 0)
                return r;

        if (g_strcmp0 (params.service, GDM_SERVICE_ID) != 0)
                return sd_varlink_error (call,
                                         "io.systemd.UserDatabase.BadService",
                                         NULL);

        store = GDM_DYNAMIC_USER_STORE (g_weak_ref_get (weak_store));
        if (store == NULL)
                return -ECANCELED;

        G_MUTEX_AUTO_LOCK (&store->mutex, locker);

        if (params.username) {
                user = g_hash_table_lookup (store->by_name, &params.username);
                if (test_group_membership (user, params.groupname))
                        return reply_user_group (call, VARLINK_REPLY_FINAL, user);
        } else {
                GHashTableIter iter;
                gpointer value;

                g_hash_table_iter_init (&iter, store->by_name);

                while (g_hash_table_iter_next (&iter, NULL, &value)) {
                        DynamicUser *next = value;

                        if (test_group_membership (next, params.groupname)) {
                                if (user != NULL) {
                                        r = reply_user_group (call, VARLINK_REPLY_MORE_FOLLOW, user);
                                        if (r < 0)
                                                return r;
                                }

                                user = next;
                        }
                }

                if (user != NULL)
                        return reply_user_group (call, VARLINK_REPLY_FINAL, user);
        }

        return sd_varlink_error (call,
                                 "io.systemd.UserDatabase.NoRecordFound",
                                 NULL);
}

static gpointer
gdm_dynamic_user_varlink_worker (gpointer data)
{
        g_autoptr (WorkerContext) ctx = data;
        g_autoptr (sd_event) event = NULL;
        g_autoptr (sd_varlink_server) server = NULL;
        int r;

        r = sd_event_default (&event);
        if (r < 0) {
                g_warning ("Failed to construct systemd event loop: %s",
                           g_strerror (-r));
                return NULL;
        }

        /* The default callback quits the event loop, which is exactly what we
         * want to happen. Also, by default sd_event doesn't take ownership of
         * the FD, which is again correct (it's owned by the Cancellable) */
        r = sd_event_add_io (event, NULL, ctx->cancel_fd, EPOLLIN, NULL, NULL);
        if (r < 0) {
                g_warning ("Failed to subscribe to cancel signal: %s",
                           g_strerror (-r));
                return NULL;
        }

        r = sd_varlink_server_new (&server, SD_VARLINK_SERVER_INHERIT_USERDATA);
        if (r < 0) {
                g_warning ("Failed to construct varlink server: %s",
                           g_strerror (-r));
                return NULL;
        }

        sd_varlink_server_set_userdata (server, &ctx->store);

        sd_varlink_server_set_info (server,
                                    "The GNOME Project",
                                    "GNOME Display Manager",
                                    VERSION,
                                    "https://gnome.org");

        r = sd_varlink_server_attach_event (server, event, SD_EVENT_PRIORITY_NORMAL);
        if (r < 0) {
                g_warning ("Failed to attach varlink server to event loop: %s",
                           g_strerror (-r));
                return NULL;
        }

        r = sd_varlink_server_listen_address (server,
                                              USERDB_SOCKET_DIR "/" GDM_SERVICE_ID,
                                              0666 | SD_VARLINK_SERVER_MODE_MKDIR_0755);
        if (r < 0) {
                g_warning ("Failed to listen on userdb socket: %s",
                           g_strerror (-r));
                return NULL;
        }

        r = sd_varlink_server_bind_method_many (server,
                                                "io.systemd.UserDatabase.GetUserRecord", vl_get_user_record,
                                                "io.systemd.UserDatabase.GetGroupRecord", vl_get_group_record,
                                                "io.systemd.UserDatabase.GetMemberships", vl_get_memberships);
        if (r < 0) {
                g_warning ("Failed to bind varlink methods: %s",
                           g_strerror (-r));
                return NULL;
        }

        r = sd_event_loop (event);
        if (r < 0)
                g_warning ("Failed to run varlink: %s",
                           g_strerror (-r));
        return NULL;
}

#else /* HAVE_USERDB */

static gpointer
gdm_dynamic_user_varlink_worker (gpointer data)
{
        /* Distros that lack systemd don't seem to have an extracted sd_varlink
         * for us to link to. But even if they provided sd_varlink, they don't
         * have the necessary userdb plumbing to dynamically register/unregister
         * users to the system. So, this is pretty much broken there.
         *
         * Theoretically, such distros should be able to just pre-define a bunch
         * of user accounts to run the greeter and initial setup:
         *
         *    `gdm-greeter`, `gdm-greeter-2`, ..., `gdm-greeter-N`, and
         *    `gnome-initial-setup`, ..., `gnome-initial-setup-N`
         *
         * GDM should be able to pick up and use these users when compiled
         * on no-systemd distros. But this is only a temporary solution, and
         * when AccountsService gets dropped in favor of userdb the same will
         * happen here. You've been warned.
         *
         * Fret not, anti-systemd folk. You should have no problem implementing
         * the userdb API (https://systemd.io/USER_GROUP_API/) outside of
         * systemd.
         */
        g_autoptr (WorkerContext) ctx = data;
        return NULL;
}

#endif /* HAVE_USERDB */
