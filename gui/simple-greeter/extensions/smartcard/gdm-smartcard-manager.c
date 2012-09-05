/* gdm-smartcard-manager.c - object for monitoring smartcard insertion and
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Written By: Ray Strode
 */
#define _GNU_SOURCE
#include "gdm-smartcard-manager.h"

#define GDM_SMARTCARD_ENABLE_INTERNAL_API
#include "gdm-smartcard.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <prerror.h>
#include <prinit.h>
#include <nss.h>
#include <pk11func.h>
#include <secmod.h>
#include <secerr.h>

#ifndef GDM_SMARTCARD_MANAGER_DRIVER
#define GDM_SMARTCARD_MANAGER_DRIVER LIBDIR"/pkcs11/libcoolkeypk11.so"
#endif

#ifndef GDM_SMARTCARD_MANAGER_NSS_DB
#define GDM_SMARTCARD_MANAGER_NSS_DB SYSCONFDIR"/pki/nssdb"
#endif

#ifndef GDM_MAX_OPEN_FILE_DESCRIPTORS
#define GDM_MAX_OPEN_FILE_DESCRIPTORS 1024
#endif

#ifndef GDM_OPEN_FILE_DESCRIPTORS_DIR
#define GDM_OPEN_FILE_DESCRIPTORS_DIR "/proc/self/fd"
#endif

typedef enum _GdmSmartcardManagerState GdmSmartcardManagerState;
typedef struct _GdmSmartcardManagerWorker GdmSmartcardManagerWorker;

enum _GdmSmartcardManagerState {
        GDM_SMARTCARD_MANAGER_STATE_STOPPED = 0,
        GDM_SMARTCARD_MANAGER_STATE_STARTING,
        GDM_SMARTCARD_MANAGER_STATE_STARTED,
        GDM_SMARTCARD_MANAGER_STATE_STOPPING,
};

struct _GdmSmartcardManagerPrivate {
        GdmSmartcardManagerState state;
        GList        *modules;
        char        *module_path;

        GList        *workers;

        GPid smartcard_event_watcher_pid;
        GHashTable *smartcards;

        guint poll_timeout_id;

        guint32 is_unstoppable : 1;
        guint32 nss_is_loaded : 1;
};

struct _GdmSmartcardManagerWorker {
        GdmSmartcardManager *manager;
        gint manager_fd;

        GThread      *thread;
        SECMODModule *module;
        GHashTable *smartcards;
        gint fd;
        GSource *event_source;

        guint32 nss_is_loaded : 1;
};

static void gdm_smartcard_manager_finalize (GObject *object);
static void gdm_smartcard_manager_class_install_signals (GdmSmartcardManagerClass *service_class);
static void gdm_smartcard_manager_class_install_properties (GdmSmartcardManagerClass *service_class);
static void gdm_smartcard_manager_set_property (GObject       *object,
                                                guint          prop_id,
                                                const GValue  *value,
                                                GParamSpec    *pspec);
static void gdm_smartcard_manager_get_property (GObject    *object,
                                                guint       prop_id,
                                                GValue     *value,
                                                GParamSpec *pspec);
static void gdm_smartcard_manager_set_module_path (GdmSmartcardManager *manager,
                                                   const char          *module_path);
static void gdm_smartcard_manager_card_removed_handler (GdmSmartcardManager *manager,
                                                        GdmSmartcard        *card);
static void gdm_smartcard_manager_card_inserted_handler (GdmSmartcardManager *manager_class,
                                                         GdmSmartcard        *card);
static gboolean gdm_smartcard_manager_stop_now (GdmSmartcardManager *manager);
static void gdm_smartcard_manager_queue_stop (GdmSmartcardManager *manager);

static GdmSmartcardManagerWorker *gdm_smartcard_manager_create_worker (GdmSmartcardManager  *manager,
                                                                       SECMODModule         *module);

static GdmSmartcardManagerWorker * gdm_smartcard_manager_worker_new (GdmSmartcardManager *manager,
                                                                     int                  worker_fd,
                                                                     int                  manager_fd,
                                                                     SECMODModule        *module);
static void gdm_smartcard_manager_worker_free (GdmSmartcardManagerWorker *worker);
static gboolean gdm_open_pipe (gint *write_fd, gint *read_fd);
static gboolean sc_read_bytes (gint fd, gpointer bytes, gsize num_bytes);
static gboolean sc_write_bytes (gint fd, gconstpointer bytes, gsize num_bytes);
static GdmSmartcard *sc_read_smartcard (gint fd, SECMODModule *module);
static gboolean sc_write_smartcard (gint fd, GdmSmartcard *card);

enum {
        PROP_0 = 0,
        PROP_MODULE_PATH,
        NUMBER_OF_PROPERTIES
};

enum {
        SMARTCARD_INSERTED = 0,
        SMARTCARD_REMOVED,
        ERROR,
        NUMBER_OF_SIGNALS
};

static guint gdm_smartcard_manager_signals[NUMBER_OF_SIGNALS];

G_DEFINE_TYPE (GdmSmartcardManager,
               gdm_smartcard_manager,
               G_TYPE_OBJECT);

static void
gdm_smartcard_manager_class_init (GdmSmartcardManagerClass *manager_class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (manager_class);

        gobject_class->finalize = gdm_smartcard_manager_finalize;

        gdm_smartcard_manager_class_install_signals (manager_class);
        gdm_smartcard_manager_class_install_properties (manager_class);

        g_type_class_add_private (manager_class,
                                  sizeof (GdmSmartcardManagerPrivate));
}

static void
gdm_smartcard_manager_class_install_properties (GdmSmartcardManagerClass *card_class)
{
        GObjectClass *object_class;
        GParamSpec *param_spec;

        object_class = G_OBJECT_CLASS (card_class);
        object_class->set_property = gdm_smartcard_manager_set_property;
        object_class->get_property = gdm_smartcard_manager_get_property;

        param_spec = g_param_spec_string ("module-path", _("Module Path"),
                                          _("path to smartcard PKCS #11 driver"),
                                          NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY);
        g_object_class_install_property (object_class, PROP_MODULE_PATH, param_spec);
}

static void
gdm_smartcard_manager_set_property (GObject       *object,
                                    guint          prop_id,
                                    const GValue  *value,
                                    GParamSpec    *pspec)
{
        GdmSmartcardManager *manager = GDM_SMARTCARD_MANAGER (object);

        switch (prop_id) {
                case PROP_MODULE_PATH:
                        gdm_smartcard_manager_set_module_path (manager,
                                                                   g_value_get_string (value));
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

static void
gdm_smartcard_manager_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
        GdmSmartcardManager *manager = GDM_SMARTCARD_MANAGER (object);
        char *module_path;

        switch (prop_id) {
                case PROP_MODULE_PATH:
                        module_path = gdm_smartcard_manager_get_module_path (manager);
                        g_value_set_string (value, module_path);
                        g_free (module_path);
                        break;

                default:
                        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                        break;
        }
}

char *
gdm_smartcard_manager_get_module_path (GdmSmartcardManager *manager)
{
        return manager->priv->module_path;
}

static void
gdm_smartcard_manager_set_module_path (GdmSmartcardManager *manager,
                                       const char          *module_path)
{
        if ((manager->priv->module_path == NULL) && (module_path == NULL)) {
                return;
        }

        if (((manager->priv->module_path == NULL) ||
         (module_path == NULL) ||
         (strcmp (manager->priv->module_path, module_path) != 0))) {
                g_free (manager->priv->module_path);
                manager->priv->module_path = g_strdup (module_path);
                g_object_notify (G_OBJECT (manager), "module-path");
        }
}

static void
gdm_smartcard_manager_card_removed_handler (GdmSmartcardManager *manager,
                                            GdmSmartcard        *card)
{
        g_debug ("informing smartcard of its removal");
        _gdm_smartcard_set_state (card, GDM_SMARTCARD_STATE_REMOVED);
        g_debug ("done");
}

static void
gdm_smartcard_manager_card_inserted_handler (GdmSmartcardManager *manager,
                                             GdmSmartcard        *card)
{
        g_debug ("informing smartcard of its insertion");

        _gdm_smartcard_set_state (card, GDM_SMARTCARD_STATE_INSERTED);
        g_debug ("done");

}

static void
gdm_smartcard_manager_class_install_signals (GdmSmartcardManagerClass *manager_class)
{
        GObjectClass *object_class;

        object_class = G_OBJECT_CLASS (manager_class);

        gdm_smartcard_manager_signals[SMARTCARD_INSERTED] =
                g_signal_new ("smartcard-inserted",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSmartcardManagerClass,
                                               smartcard_inserted),
                              NULL, NULL, g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);
        manager_class->smartcard_inserted = gdm_smartcard_manager_card_inserted_handler;

        gdm_smartcard_manager_signals[SMARTCARD_REMOVED] =
                g_signal_new ("smartcard-removed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (GdmSmartcardManagerClass,
                                               smartcard_removed),
                              NULL, NULL, g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);
        manager_class->smartcard_removed = gdm_smartcard_manager_card_removed_handler;

        gdm_smartcard_manager_signals[ERROR] =
                g_signal_new ("error",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmSmartcardManagerClass, error),
                              NULL, NULL, g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE, 1, G_TYPE_POINTER);
        manager_class->error = NULL;
}

static gboolean
sc_slot_id_equal (CK_SLOT_ID *slot_id_1,
                      CK_SLOT_ID *slot_id_2)
{
        g_assert (slot_id_1 != NULL);
        g_assert (slot_id_2 != NULL);

        return *slot_id_1 == *slot_id_2;
}

static gboolean
sc_slot_id_hash (CK_SLOT_ID *slot_id)
{
        guint32 upper_bits, lower_bits;
        gint temp;

        if (sizeof (CK_SLOT_ID) == sizeof (gint)) {
                return g_int_hash (slot_id);
        }

        upper_bits = ((*slot_id) >> 31) - 1;
        lower_bits = (*slot_id) & 0xffffffff;

        /* The upper bits are almost certainly always zero,
         * so let's degenerate to g_int_hash for the
         * (very) common case
         */
        temp = lower_bits + upper_bits;
        return upper_bits + g_int_hash (&temp);
}

static void
gdm_smartcard_manager_init (GdmSmartcardManager *manager)
{
        g_debug ("initializing smartcard manager");

        manager->priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
                                                     GDM_TYPE_SMARTCARD_MANAGER,
                                                     GdmSmartcardManagerPrivate);
        manager->priv->poll_timeout_id = 0;
        manager->priv->is_unstoppable = FALSE;

        manager->priv->smartcards =
                g_hash_table_new_full (g_str_hash,
                                       g_str_equal,
                                       (GDestroyNotify) g_free,
                                       (GDestroyNotify) g_object_unref);

        if (!g_thread_supported()) {
                g_thread_init (NULL);
        }

}

static void
gdm_smartcard_manager_finalize (GObject *object)
{
        GdmSmartcardManager *manager;
        GObjectClass *gobject_class;

        manager = GDM_SMARTCARD_MANAGER (object);
        gobject_class =
                G_OBJECT_CLASS (gdm_smartcard_manager_parent_class);

        gdm_smartcard_manager_stop_now (manager);

        g_hash_table_destroy (manager->priv->smartcards);
        manager->priv->smartcards = NULL;

        gobject_class->finalize (object);
}

GQuark
gdm_smartcard_manager_error_quark (void)
{
        static GQuark error_quark = 0;

        if (error_quark == 0) {
                error_quark = g_quark_from_static_string ("gdm-smartcard-manager-error-quark");
        }

        return error_quark;
}

GdmSmartcardManager *
gdm_smartcard_manager_new (const char *module_path)
{
        GdmSmartcardManager *instance;

        instance = GDM_SMARTCARD_MANAGER (g_object_new (GDM_TYPE_SMARTCARD_MANAGER,
                                                        "module-path", module_path,
                                                        NULL));

        return instance;
}

static void
gdm_smartcard_manager_emit_error (GdmSmartcardManager *manager,
                                  GError              *error)
{
        manager->priv->is_unstoppable = TRUE;
        g_signal_emit (manager, gdm_smartcard_manager_signals[ERROR], 0,
                       error);
        manager->priv->is_unstoppable = FALSE;
}

static void
gdm_smartcard_manager_emit_smartcard_inserted (GdmSmartcardManager *manager,
                                               GdmSmartcard        *card)
{
        manager->priv->is_unstoppable = TRUE;
        g_signal_emit (manager, gdm_smartcard_manager_signals[SMARTCARD_INSERTED], 0,
                       card);
        manager->priv->is_unstoppable = FALSE;
}

static void
gdm_smartcard_manager_emit_smartcard_removed (GdmSmartcardManager *manager,
                                              GdmSmartcard        *card)
{
        manager->priv->is_unstoppable = TRUE;
        g_signal_emit (manager, gdm_smartcard_manager_signals[SMARTCARD_REMOVED], 0,
                       card);
        manager->priv->is_unstoppable = FALSE;
}

static gboolean
gdm_smartcard_manager_check_for_and_process_events (GIOChannel          *io_channel,
                                                    GIOCondition         condition,
                                                    GdmSmartcardManagerWorker *worker)
{
        GdmSmartcard *card;
        GdmSmartcardManager *manager;
        gboolean should_stop;
        guchar event_type;
        char *card_name;
        gint fd;

        manager = worker->manager;

        g_debug ("event!");
        card = NULL;
        should_stop = (condition & G_IO_HUP) || (condition & G_IO_ERR);

        if (should_stop) {
                g_debug ("received %s on event socket, stopping "
                          "manager...",
                          (condition & G_IO_HUP) && (condition & G_IO_ERR)?
                          "error and hangup" :
                          (condition & G_IO_HUP)?
                          "hangup" : "error");
        }

        if (!(condition & G_IO_IN)) {
                g_debug ("nevermind outta here!");
                goto out;
        }

        fd = g_io_channel_unix_get_fd (io_channel);

        event_type = '\0';
        if (!sc_read_bytes (fd, &event_type, 1)) {
                g_debug ("could not read event type, stopping");
                should_stop = TRUE;
                goto out;
        }

        card = sc_read_smartcard (fd, worker->module);

        if (card == NULL) {
                g_debug ("could not read card, stopping");
                should_stop = TRUE;
                goto out;
        }

        card_name = gdm_smartcard_get_name (card);
        g_debug ("card '%s' had event %c", card_name, event_type);

        switch (event_type) {
                case 'I':
                        g_hash_table_replace (manager->priv->smartcards,
                                              card_name, card);
                        card_name = NULL;

                        gdm_smartcard_manager_emit_smartcard_inserted (manager, card);
                        card = NULL;
                        break;

                case 'R':
                        gdm_smartcard_manager_emit_smartcard_removed (manager, card);
                        if (!g_hash_table_remove (manager->priv->smartcards, card_name)) {
                                g_debug ("got removal event of unknown card!");
                        }
                        g_free (card_name);
                        card_name = NULL;
                        card = NULL;
                        break;

                default:
                        g_free (card_name);
                        card_name = NULL;
                        g_object_unref (card);

                        should_stop = TRUE;
                        break;
        }

out:
        if (should_stop) {
                GError *error;

                error = g_error_new (GDM_SMARTCARD_MANAGER_ERROR,
                                     GDM_SMARTCARD_MANAGER_ERROR_WATCHING_FOR_EVENTS,
                                     "%s", (condition & G_IO_IN) ? g_strerror (errno) : _("received error or hang up from event source"));

                gdm_smartcard_manager_emit_error (manager, error);
                g_error_free (error);
                gdm_smartcard_manager_stop_now (manager);
                return FALSE;
        }

        return TRUE;
}

static void
stop_manager (GdmSmartcardManager *manager)
{
        manager->priv->state = GDM_SMARTCARD_MANAGER_STATE_STOPPED;

        if (manager->priv->nss_is_loaded) {
                NSS_Shutdown ();
                manager->priv->nss_is_loaded = FALSE;
        }
        g_debug ("smartcard manager stopped");
}

static void
stop_worker (GdmSmartcardManagerWorker *worker)
{
        GdmSmartcardManager *manager;

        manager = worker->manager;

        if (worker->event_source != NULL) {
                g_source_destroy (worker->event_source);
                worker->event_source = NULL;
        }

        if (worker->thread != NULL) {
                SECMOD_CancelWait (worker->module);
                worker->thread = NULL;
        }

        SECMOD_DestroyModule (worker->module);
        manager->priv->workers = g_list_remove (manager->priv->workers, worker);

        if (manager->priv->workers == NULL && manager->priv->state != GDM_SMARTCARD_MANAGER_STATE_STOPPED) {
                stop_manager (manager);
        }
}

static void
gdm_smartcard_manager_event_processing_stopped_handler (GdmSmartcardManagerWorker *worker)
{
        worker->event_source = NULL;

        stop_worker (worker);
}

static gboolean
gdm_open_pipe (gint *write_fd,
                  gint *read_fd)
{
        gint pipe_fds[2] = { -1, -1 };

        g_assert (write_fd != NULL);
        g_assert (read_fd != NULL);

        if (pipe (pipe_fds) < 0) {
                return FALSE;
        }

        if (fcntl (pipe_fds[0], F_SETFD, FD_CLOEXEC) < 0) {
                close (pipe_fds[0]);
                close (pipe_fds[1]);
                return FALSE;
        }

        if (fcntl (pipe_fds[1], F_SETFD, FD_CLOEXEC) < 0) {
                close (pipe_fds[0]);
                close (pipe_fds[1]);
                return FALSE;
        }

        *read_fd = pipe_fds[0];
        *write_fd = pipe_fds[1];

        return TRUE;
}

static void
gdm_smartcard_manager_stop_watching_for_events (GdmSmartcardManager  *manager)
{
        GList *node;

        node = manager->priv->workers;
        while (node != NULL) {
                GdmSmartcardManagerWorker *worker;
                GList *next_node;

                worker = (GdmSmartcardManagerWorker *) node->data;
                next_node = node->next;

                stop_worker (worker);

                node = next_node;
        }
}

static gboolean
sc_load_nss (GError **error)
{
        SECStatus status = SECSuccess;
        static const guint32 flags =
        NSS_INIT_READONLY |
        NSS_INIT_FORCEOPEN | NSS_INIT_NOROOTINIT |
        NSS_INIT_OPTIMIZESPACE | NSS_INIT_PK11RELOAD;

        g_debug ("attempting to load NSS database '%s'",
                 GDM_SMARTCARD_MANAGER_NSS_DB);

        PR_Init (PR_USER_THREAD, PR_PRIORITY_NORMAL, 0);

        status = NSS_Initialize (GDM_SMARTCARD_MANAGER_NSS_DB,
                                 "", "", SECMOD_DB, flags);

        if (status != SECSuccess) {
                gsize error_message_size;
                char *error_message;

                error_message_size = PR_GetErrorTextLength ();

                if (error_message_size == 0) {
                        g_debug ("NSS security system could not be initialized");
                        g_set_error (error,
                                     GDM_SMARTCARD_MANAGER_ERROR,
                                     GDM_SMARTCARD_MANAGER_ERROR_WITH_NSS,
                                     _("NSS security system could not be initialized"));
                        goto out;
                }

                error_message = g_slice_alloc0 (error_message_size);
                PR_GetErrorText (error_message);

                g_set_error (error,
                             GDM_SMARTCARD_MANAGER_ERROR,
                             GDM_SMARTCARD_MANAGER_ERROR_WITH_NSS,
                             "%s", error_message);
                g_debug ("NSS security system could not be initialized - %s",
                          error_message);

                g_slice_free1 (error_message_size, error_message);

                goto out;
        }

        g_debug ("NSS database sucessfully loaded");
        return TRUE;

out:
        g_debug ("NSS database couldn't be sucessfully loaded");
        return FALSE;
}

static GList *
get_available_modules (GdmSmartcardManager  *manager)
{
        SECMODModuleList *module_list, *tmp;
        GList *modules;

        g_debug ("Getting list of suitable modules");

        module_list = SECMOD_GetDefaultModuleList ();
        modules = NULL;
        for (tmp = module_list; tmp != NULL; tmp = tmp->next) {
                if (!SECMOD_HasRemovableSlots (tmp->module) ||
                    !tmp->module->loaded)
                        continue;

                g_debug ("Using module '%s'", tmp->module->commonName);

                modules = g_list_prepend (modules,
                                          SECMOD_ReferenceModule (tmp->module));
        }

        return modules;
}

static gboolean
load_driver (GdmSmartcardManager  *manager,
             char                 *module_path,
             GError              **error)
{
        GList *modules;
        char *module_spec;
        gboolean module_explicitly_specified;

        g_debug ("attempting to load driver...");

        modules = NULL;
        module_explicitly_specified = module_path != NULL;
        if (module_explicitly_specified) {
                SECMODModule *module;

                module_spec = g_strdup_printf ("library=\"%s\"", module_path);
                g_debug ("loading smartcard driver using spec '%s'",
                          module_spec);

                module = SECMOD_LoadUserModule (module_spec,
                                                NULL /* parent */,
                                                FALSE /* recurse */);
                g_free (module_spec);
                module_spec = NULL;

                if (!SECMOD_HasRemovableSlots (module) ||
                    !module->loaded) {
                        modules = g_list_prepend (modules, module);
                } else {
                        g_debug ("fallback module found but not %s",
                                 SECMOD_HasRemovableSlots (module)?
                                 "removable" : "loaded");
                        SECMOD_DestroyModule (module);
                }

        } else {
                SECMODListLock *lock;

                lock = SECMOD_GetDefaultModuleListLock ();

                if (lock != NULL) {
                        SECMOD_GetReadLock (lock);
                        modules = get_available_modules (manager);
                        SECMOD_ReleaseReadLock (lock);
                }

                /* fallback to compiled in driver path
                 */
                if (modules == NULL) {
                        SECMODModule *module;
                        module_path = GDM_SMARTCARD_MANAGER_DRIVER;
                        module_spec = g_strdup_printf ("library=\"%s\"", module_path);
                        g_debug ("loading smartcard driver using spec '%s'",
                                module_spec);

                        module = SECMOD_LoadUserModule (module_spec,
                                NULL /* parent */,
                                FALSE /* recurse */);
                        g_free (module_spec);
                        module_spec = NULL;

                        if (!SECMOD_HasRemovableSlots (module) ||
                            !module->loaded) {
                                modules = g_list_prepend (modules, module);
                        } else {
                                g_debug ("fallback module found but not loaded");
                                SECMOD_DestroyModule (module);
                        }
                }

        }

        if (!module_explicitly_specified && modules == NULL) {
                g_set_error (error,
                             GDM_SMARTCARD_MANAGER_ERROR,
                             GDM_SMARTCARD_MANAGER_ERROR_LOADING_DRIVER,
                             _("no suitable smartcard driver could be found"));
        } else if (modules == NULL) {

                gsize error_message_size;
                char *error_message;

                error_message_size = PR_GetErrorTextLength ();

                if (error_message_size == 0) {
                        g_debug ("smartcard driver '%s' could not be loaded",
                                  module_path);
                        g_set_error (error,
                                     GDM_SMARTCARD_MANAGER_ERROR,
                                     GDM_SMARTCARD_MANAGER_ERROR_LOADING_DRIVER,
                                     _("smartcard driver '%s' could not be "
                                       "loaded"), module_path);
                        goto out;
                }

                error_message = g_slice_alloc0 (error_message_size);
                PR_GetErrorText (error_message);

                g_set_error (error,
                             GDM_SMARTCARD_MANAGER_ERROR,
                             GDM_SMARTCARD_MANAGER_ERROR_LOADING_DRIVER,
                             "%s", error_message);

                g_debug ("smartcard driver '%s' could not be loaded - %s",
                          module_path, error_message);
                g_slice_free1 (error_message_size, error_message);
        }

        manager->priv->modules = modules;
out:
        return manager->priv->modules != NULL;
}

static void
gdm_smartcard_manager_get_all_cards (GdmSmartcardManager *manager)
{
        GList *node;
        int i;

        node = manager->priv->workers;
        while (node != NULL) {

                GdmSmartcardManagerWorker *worker;

                worker = (GdmSmartcardManagerWorker *) node->data;

                for (i = 0; i < worker->module->slotCount; i++) {
                        GdmSmartcard *card;
                        CK_SLOT_ID    slot_id;
                        gint          slot_series;
                        char         *card_name;

                        slot_id = PK11_GetSlotID (worker->module->slots[i]);
                        slot_series = PK11_GetSlotSeries (worker->module->slots[i]);

                        card = _gdm_smartcard_new (worker->module,
                                                   slot_id, slot_series);

                        card_name = gdm_smartcard_get_name (card);

                        g_hash_table_replace (manager->priv->smartcards,
                                              card_name, card);
                }
                node = node->next;
        }
}

static GdmSmartcardManagerWorker *
start_worker (GdmSmartcardManager  *manager,
              SECMODModule         *module,
              GError              **error)
{
        GIOChannel *io_channel;
        GSource *source;
        GdmSmartcardManagerWorker *worker;

        worker = gdm_smartcard_manager_create_worker (manager, module);

        if (worker == NULL) {
                g_set_error (error,
                             GDM_SMARTCARD_MANAGER_ERROR,
                             GDM_SMARTCARD_MANAGER_ERROR_WATCHING_FOR_EVENTS,
                             _("could not watch for incoming card events - %s"),
                             g_strerror (errno));

                goto out;
        }

        io_channel = g_io_channel_unix_new (worker->manager_fd);

        source = g_io_create_watch (io_channel, G_IO_IN | G_IO_HUP);
        g_io_channel_unref (io_channel);
        io_channel = NULL;

        worker->event_source = source;

        g_source_set_callback (worker->event_source,
                               (GSourceFunc) (GIOFunc)
                               gdm_smartcard_manager_check_for_and_process_events,
                               worker,
                               (GDestroyNotify)
                               gdm_smartcard_manager_event_processing_stopped_handler);
        g_source_attach (worker->event_source, NULL);
        g_source_unref (worker->event_source);
out:
        return worker;
}

static void
start_workers (GdmSmartcardManager *manager)
{
        GList        *node;

        node = manager->priv->modules;
        while (node != NULL) {
                SECMODModule *module;
                GdmSmartcardManagerWorker *worker;
                GError *error;

                module = (SECMODModule *) node->data;

                error = NULL;
                worker = start_worker (manager, module, &error);
                if (worker == NULL) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                } else {
                        manager->priv->workers = g_list_prepend (manager->priv->workers,
                                                                 worker);
                }
                node = node->next;
        }
}

gboolean
gdm_smartcard_manager_start (GdmSmartcardManager  *manager,
                             GError              **error)
{
        GError *nss_error;

        if (manager->priv->state == GDM_SMARTCARD_MANAGER_STATE_STARTED) {
                g_debug ("smartcard manager already started");
                return TRUE;
        }

        manager->priv->state = GDM_SMARTCARD_MANAGER_STATE_STARTING;

        nss_error = NULL;
        if (!manager->priv->nss_is_loaded && !sc_load_nss (&nss_error)) {
                g_propagate_error (error, nss_error);
                goto out;
        }
        manager->priv->nss_is_loaded = TRUE;

        if (manager->priv->modules == NULL) {
                if (!load_driver (manager, manager->priv->module_path, &nss_error)) {
                        g_propagate_error (error, nss_error);
                        goto out;
                }
        }

        start_workers (manager);

        /* populate the hash with cards that are already inserted
         */
        gdm_smartcard_manager_get_all_cards (manager);

        manager->priv->state = GDM_SMARTCARD_MANAGER_STATE_STARTED;

out:
        /* don't leave it in a half started state
         */
        if (manager->priv->state != GDM_SMARTCARD_MANAGER_STATE_STARTED) {
                g_debug ("smartcard manager could not be completely started");
                gdm_smartcard_manager_stop (manager);
        } else {
                g_debug ("smartcard manager started");
        }

        return manager->priv->state == GDM_SMARTCARD_MANAGER_STATE_STARTED;
}

static gboolean
gdm_smartcard_manager_stop_now (GdmSmartcardManager *manager)
{
        if (manager->priv->state == GDM_SMARTCARD_MANAGER_STATE_STOPPED) {
                return FALSE;
        }

        gdm_smartcard_manager_stop_watching_for_events (manager);

        return FALSE;
}

static void
gdm_smartcard_manager_queue_stop (GdmSmartcardManager *manager)
{

        manager->priv->state = GDM_SMARTCARD_MANAGER_STATE_STOPPING;

        g_idle_add ((GSourceFunc) gdm_smartcard_manager_stop_now, manager);
}

void
gdm_smartcard_manager_stop (GdmSmartcardManager *manager)
{
        if (manager->priv->state == GDM_SMARTCARD_MANAGER_STATE_STOPPED) {
                return;
        }

        if (manager->priv->is_unstoppable) {
                gdm_smartcard_manager_queue_stop (manager);
                return;
        }

        gdm_smartcard_manager_stop_now (manager);
}

static GdmSmartcardManagerWorker *
gdm_smartcard_manager_worker_new (GdmSmartcardManager *manager,
                                  gint                 worker_fd,
                                  gint                 manager_fd,
                                  SECMODModule        *module)
{
        GdmSmartcardManagerWorker *worker;

        worker = g_slice_new0 (GdmSmartcardManagerWorker);
        worker->manager = manager;
        worker->fd = worker_fd;
        worker->manager_fd = manager_fd;
        worker->module = module;

        worker->smartcards =
                g_hash_table_new_full ((GHashFunc) sc_slot_id_hash,
                                       (GEqualFunc) sc_slot_id_equal,
                                       (GDestroyNotify) g_free,
                                       (GDestroyNotify) g_object_unref);

        return worker;
}

static void
gdm_smartcard_manager_worker_free (GdmSmartcardManagerWorker *worker)
{
        if (worker->smartcards != NULL) {
                g_hash_table_destroy (worker->smartcards);
                worker->smartcards = NULL;
        }

        g_slice_free (GdmSmartcardManagerWorker, worker);
}

static gboolean
sc_read_bytes (gint fd, gpointer bytes, gsize num_bytes)
{
        size_t bytes_left;
        size_t total_bytes_read;
        ssize_t bytes_read;

        bytes_left = (size_t) num_bytes;
        total_bytes_read = 0;

        do {
                bytes_read = read (fd, (gchar *) bytes + total_bytes_read, bytes_left);
                g_assert (bytes_read <= (ssize_t) bytes_left);

                if (bytes_read <= 0) {
                        if ((bytes_read < 0) && (errno == EINTR || errno == EAGAIN)) {
                                continue;
                        }

                        bytes_left = 0;
                } else {
                        bytes_left -= bytes_read;
                        total_bytes_read += bytes_read;
                }
        } while (bytes_left > 0);

        if (total_bytes_read <  (size_t) num_bytes) {
                return FALSE;
        }

        return TRUE;
}

static gboolean
sc_write_bytes (gint fd, gconstpointer bytes, gsize num_bytes)
{
        size_t bytes_left;
        size_t total_bytes_written;
        ssize_t bytes_written;

        bytes_left = (size_t) num_bytes;
        total_bytes_written = 0;

        do {
                bytes_written = write (fd, (gchar *) bytes + total_bytes_written, bytes_left);
                g_assert (bytes_written <= (ssize_t) bytes_left);

                if (bytes_written <= 0) {
                        if ((bytes_written < 0) && (errno == EINTR || errno == EAGAIN)) {
                                continue;
                        }

                        bytes_left = 0;
                } else {
                        bytes_left -= bytes_written;
                        total_bytes_written += bytes_written;
                }
        } while (bytes_left > 0);

        if (total_bytes_written <  (size_t) num_bytes) {
                return FALSE;
        }

        return TRUE;
}

static GdmSmartcard *
sc_read_smartcard (gint          fd,
                   SECMODModule *module)
{
        GdmSmartcard *card;
        char *card_name;
        gsize card_name_size;

        card_name_size = 0;
        if (!sc_read_bytes (fd, &card_name_size, sizeof (card_name_size))) {
                return NULL;
        }

        card_name = g_slice_alloc0 (card_name_size);
        if (!sc_read_bytes (fd, card_name, card_name_size)) {
                g_slice_free1 (card_name_size, card_name);
                return NULL;
        }
        card = _gdm_smartcard_new_from_name (module, card_name);
        g_slice_free1 (card_name_size, card_name);

        return card;
}

static gboolean
sc_write_smartcard (gint          fd,
                    GdmSmartcard *card)
{
        gsize card_name_size;
        char *card_name;

        card_name = gdm_smartcard_get_name (card);
        card_name_size = strlen (card_name) + 1;

        if (!sc_write_bytes (fd, &card_name_size, sizeof (card_name_size))) {
                g_free (card_name);
                return FALSE;
        }

        if (!sc_write_bytes (fd, card_name, card_name_size)) {
                g_free (card_name);
                return FALSE;
        }
        g_free (card_name);

        return TRUE;
}

static gboolean
gdm_smartcard_manager_worker_emit_smartcard_removed (GdmSmartcardManagerWorker  *worker,
                                                     GdmSmartcard               *card,
                                                     GError                    **error)
{
        g_debug ("card '%s' removed!", gdm_smartcard_get_name (card));

        if (!sc_write_bytes (worker->fd, "R", 1)) {
                goto error_out;
        }

        if (!sc_write_smartcard (worker->fd, card)) {
                goto error_out;
        }

        return TRUE;

error_out:
        g_set_error (error, GDM_SMARTCARD_MANAGER_ERROR,
                     GDM_SMARTCARD_MANAGER_ERROR_REPORTING_EVENTS,
                     "%s", g_strerror (errno));
        return FALSE;
}

static gboolean
gdm_smartcard_manager_worker_emit_smartcard_inserted (GdmSmartcardManagerWorker  *worker,
                                                      GdmSmartcard               *card,
                                                      GError                    **error)
{

        g_debug ("card '%s' inserted!", gdm_smartcard_get_name (card));
        if (!sc_write_bytes (worker->fd, "I", 1)) {
                goto error_out;
        }

        if (!sc_write_smartcard (worker->fd, card)) {
                goto error_out;
        }

        return TRUE;

error_out:
        g_set_error (error, GDM_SMARTCARD_MANAGER_ERROR,
                     GDM_SMARTCARD_MANAGER_ERROR_REPORTING_EVENTS,
                     "%s", g_strerror (errno));
        return FALSE;
}

static gboolean
gdm_smartcard_manager_worker_watch_for_and_process_event (GdmSmartcardManagerWorker  *worker,
                                                          GError                    **error)
{
        PK11SlotInfo *slot;
        CK_SLOT_ID slot_id, *key;
        gint slot_series, card_slot_series;
        GdmSmartcard *card;
        GError *processing_error;

        g_debug ("waiting for card event");

        /* FIXME: we return FALSE quite a bit in this function without cleaning up
         * resources.  By returning FALSE we're going to ultimately exit anyway, but
         * we should still be tidier about things.
         */

        slot = SECMOD_WaitForAnyTokenEvent (worker->module, 0, PR_SecondsToInterval (1));

        processing_error = NULL;

        if (slot == NULL) {
                int error_code;

                error_code = PORT_GetError ();
                if ((error_code == 0) || (error_code == SEC_ERROR_NO_EVENT)) {
                        g_debug ("spurrious event occurred");
                        return TRUE;
                }

                /* FIXME: is there a function to convert from a PORT error
                 * code to a translated string?
                 */
                g_set_error (error, GDM_SMARTCARD_MANAGER_ERROR,
                             GDM_SMARTCARD_MANAGER_ERROR_WITH_NSS,
                             _("encountered unexpected error while "
                               "waiting for smartcard events"));
                return FALSE;
        }

        /* the slot id and series together uniquely identify a card.
         * You can never have two cards with the same slot id at the
         * same time, however (I think), so we can key off of it.
         */
        slot_id = PK11_GetSlotID (slot);
        slot_series = PK11_GetSlotSeries (slot);

        /* First check to see if there is a card that we're currently
         * tracking in the slot.
         */
        key = g_new (CK_SLOT_ID, 1);
        *key = slot_id;
        card = g_hash_table_lookup (worker->smartcards, key);

        if (card != NULL) {
                card_slot_series = gdm_smartcard_get_slot_series (card);
        } else {
                card_slot_series = -1;
        }

        if (PK11_IsPresent (slot)) {
                /* Now, check to see if their is a new card in the slot.
                 * If there was a different card in the slot now than
                 * there was before, then we need to emit a removed signal
                 * for the old card (we don't want unpaired insertion events).
                 */
                if ((card != NULL) &&
                    card_slot_series != slot_series) {
                        if (!gdm_smartcard_manager_worker_emit_smartcard_removed (worker, card, &processing_error)) {
                                g_propagate_error (error, processing_error);
                                return FALSE;
                        }
                }

                card = _gdm_smartcard_new (worker->module,
                                           slot_id, slot_series);

                g_hash_table_replace (worker->smartcards,
                                      key, card);
                key = NULL;

                if (!gdm_smartcard_manager_worker_emit_smartcard_inserted (worker, card, &processing_error)) {
                        g_propagate_error (error, processing_error);
                        return FALSE;
                }
        } else {
                /* if we aren't tracking the card, just discard the event.
                 * We don't want unpaired remove events.  Note on startup
                 * NSS will generate an "insertion" event if a card is
                 * already inserted in the slot.
                 */
                if ((card != NULL)) {
                        /* FIXME: i'm not sure about this code.  Maybe we
                         * shouldn't do this at all, or maybe we should do it
                         * n times (where n = slot_series - card_slot_series + 1)
                         *
                         * Right now, i'm just doing it once.
                         */
                        if ((slot_series - card_slot_series) > 1) {

                                if (!gdm_smartcard_manager_worker_emit_smartcard_removed (worker, card, &processing_error)) {
                                        g_propagate_error (error, processing_error);
                                        return FALSE;
                                }
                                g_hash_table_remove (worker->smartcards, key);

                                card = _gdm_smartcard_new (worker->module,
                                                                slot_id, slot_series);
                                g_hash_table_replace (worker->smartcards,
                                                      key, card);
                                key = NULL;
                                if (!gdm_smartcard_manager_worker_emit_smartcard_inserted (worker, card, &processing_error)) {
                                        g_propagate_error (error, processing_error);
                                        return FALSE;
                                }
                        }

                        if (!gdm_smartcard_manager_worker_emit_smartcard_removed (worker, card, &processing_error)) {
                                g_propagate_error (error, processing_error);
                                return FALSE;
                        }

                        g_hash_table_remove (worker->smartcards, key);
                        card = NULL;
                } else {
                        g_debug ("got spurious remove event");
                }
        }

        g_free (key);
        PK11_FreeSlot (slot);

        return TRUE;
}

static void
gdm_smartcard_manager_worker_run (GdmSmartcardManagerWorker *worker)
{
        GError *error;
        gboolean should_continue;

        do
        {
                error = NULL;
                should_continue = gdm_smartcard_manager_worker_watch_for_and_process_event (worker, &error);
        }
        while (should_continue);

        if (error != NULL)  {
                g_debug ("could not process card event - %s", error->message);
                g_error_free (error);
        }

        gdm_smartcard_manager_worker_free (worker);
}

static GdmSmartcardManagerWorker *
gdm_smartcard_manager_create_worker (GdmSmartcardManager  *manager,
                                     SECMODModule         *module)
{
        GdmSmartcardManagerWorker *worker;
        gint write_fd, read_fd;

        write_fd = -1;
        read_fd = -1;
        if (!gdm_open_pipe (&write_fd, &read_fd)) {
                return FALSE;
        }

        worker = gdm_smartcard_manager_worker_new (manager,
                                                   write_fd,
                                                   read_fd,
                                                   module);

        worker->thread = g_thread_create ((GThreadFunc)
                                          gdm_smartcard_manager_worker_run,
                                          worker, FALSE, NULL);

        if (worker->thread == NULL) {
                gdm_smartcard_manager_worker_free (worker);
                return NULL;
        }

        return worker;
}

#ifdef GDM_SMARTCARD_MANAGER_ENABLE_TEST
#include <glib.h>

static GMainLoop *event_loop;
static gboolean should_exit_on_next_remove = FALSE;

static gboolean
on_timeout (GdmSmartcardManager *manager)
{
        GError *error = NULL;
        g_print ("Re-enabling manager.\n");

        if (!gdm_smartcard_manager_start (manager, &error)) {
                g_warning ("could not start smartcard manager - %s",
                           error->message);
                g_error_free (error);
                return 1;
        }
        g_print ("Please re-insert smartcard\n");

        should_exit_on_next_remove = TRUE;

        return FALSE;
}

static void
on_device_inserted (GdmSmartcardManager *manager,
                    GdmSmartcard        *card)
{
        g_print ("smartcard inserted!\n");
        g_print ("Please remove it.\n");
}

static void
on_device_removed (GdmSmartcardManager *manager,
                   GdmSmartcard        *card)
{
        g_print ("smartcard removed!\n");

        if (should_exit_on_next_remove) {
                g_main_loop_quit (event_loop);
        } else {
                g_print ("disabling manager for 2 seconds\n");
                gdm_smartcard_manager_stop (manager);
                g_timeout_add (2000, (GSourceFunc) on_timeout, manager);
        }
}

int
main (int   argc,
      char *argv[])
{
        GdmSmartcardManager *manager;
        GError *error;

        g_log_set_always_fatal (G_LOG_LEVEL_ERROR
                                | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING);

        g_type_init ();

        g_message ("creating instance of 'smartcard manager' object...");
        manager = gdm_smartcard_manager_new (NULL);
        g_message ("'smartcard manager' object created successfully");

        g_signal_connect (manager, "smartcard-inserted",
                          G_CALLBACK (on_device_inserted), NULL);

        g_signal_connect (manager, "smartcard-removed",
                          G_CALLBACK (on_device_removed), NULL);

        g_message ("starting listener...");

        error = NULL;
        if (!gdm_smartcard_manager_start (manager, &error)) {
                g_warning ("could not start smartcard manager - %s",
                           error->message);
                g_error_free (error);
                return 1;
        }

        event_loop = g_main_loop_new (NULL, FALSE);
        g_main_loop_run (event_loop);
        g_main_loop_unref (event_loop);
        event_loop = NULL;

        g_message ("destroying previously created 'smartcard manager' object...");
        g_object_unref (manager);
        manager = NULL;
        g_message ("'smartcard manager' object destroyed successfully");

        return 0;
}
#endif
