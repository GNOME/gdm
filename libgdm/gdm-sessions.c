/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright 2008 Red Hat, Inc,
 *           2007 William Jon McCann <mccann@jhu.edu>
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
 * Written by : William Jon McCann <mccann@jhu.edu>
 *              Ray Strode <rstrode@redhat.com>
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <systemd/sd-login.h>

#include "gdm-sessions.h"

typedef struct _GdmSessionFile {
        char    *id;
        char    *path;
        char    *translated_name;
        char    *translated_comment;
} GdmSessionFile;

static GHashTable *gdm_available_sessions_map;

static gboolean gdm_sessions_map_is_initialized = FALSE;

static void
gdm_session_file_free (GdmSessionFile *session)
{
  g_free (session->id);
  g_free (session->path);
  g_free (session->translated_name);
  g_free (session->translated_comment);
  g_free (session);
}

static char *
get_systemd_session (void)
{
        int ret;
        g_autofree char *systemd_unit = NULL;
        g_autofree char *session_id = NULL;
        pid_t pid;
        uid_t uid;

        pid = getpid ();
        uid = getuid ();

        ret = sd_pid_get_user_unit (pid, &systemd_unit);

        if (ret == 0)
                ret = sd_uid_get_display (uid, &session_id);
        else
                ret = sd_pid_get_session (pid, &session_id);

        if (ret < 0)
                return NULL;

        return g_steal_pointer (&session_id);
}

static char *
get_systemd_seat (void)
{
        g_autofree char *session_id = NULL;
        g_autofree char *seat = NULL;
        int ret;

        session_id = get_systemd_session ();

        if (session_id == NULL)
                return NULL;

        ret = sd_session_get_seat (session_id, &seat);

        if (ret != 0)
                return NULL;

        return g_steal_pointer (&seat);
}

/* adapted from gnome-menus desktop-entries.c */
static gboolean
key_file_is_relevant (GKeyFile     *key_file)
{
        GError    *error;
        g_autofree char *seat = NULL;
        gboolean   no_display;
        gboolean   hidden;
        gboolean   tryexec_failed;
        gboolean   can_run_headless;
        gboolean   only_headless_allowed;
        char      *tryexec;

        error = NULL;
        no_display = g_key_file_get_boolean (key_file,
                                             G_KEY_FILE_DESKTOP_GROUP,
                                             "NoDisplay",
                                             &error);
        if (error) {
                no_display = FALSE;
                g_error_free (error);
        }

        error = NULL;
        hidden = g_key_file_get_boolean (key_file,
                                         G_KEY_FILE_DESKTOP_GROUP,
                                         "Hidden",
                                         &error);
        if (error) {
                hidden = FALSE;
                g_error_free (error);
        }

        seat = get_systemd_seat ();

        only_headless_allowed = seat == NULL;

        can_run_headless = g_key_file_get_boolean (key_file,
                                                   G_KEY_FILE_DESKTOP_GROUP,
                                                   "X-GDM-CanRunHeadless",
                                                   NULL);

        tryexec_failed = FALSE;
        tryexec = g_key_file_get_string (key_file,
                                         G_KEY_FILE_DESKTOP_GROUP,
                                         "TryExec",
                                         NULL);
        if (tryexec) {
                char *path;

                path = g_find_program_in_path (g_strstrip (tryexec));

                tryexec_failed = (path == NULL);

                g_free (path);
                g_free (tryexec);
        }

        if (no_display || hidden || tryexec_failed || (only_headless_allowed && !can_run_headless)) {
                return FALSE;
        }

        return TRUE;
}

static void
load_session_file (const char              *id,
                   const char              *path)
{
        GKeyFile          *key_file;
        GError            *error;
        gboolean           res;
        GdmSessionFile    *session;

        key_file = g_key_file_new ();

        error = NULL;
        res = g_key_file_load_from_file (key_file, path, 0, &error);

        if (!res) {
                g_debug ("Failed to load \"%s\": %s\n", path, error->message);
                g_error_free (error);
                goto out;
        }

        if (! g_key_file_has_group (key_file, G_KEY_FILE_DESKTOP_GROUP)) {
                goto out;
        }

        res = g_key_file_has_key (key_file, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL);
        if (! res) {
                g_debug ("\"%s\" contains no \"Name\" key\n", path);
                goto out;
        }

        if (!key_file_is_relevant (key_file)) {
                g_debug ("\"%s\" is hidden, contains non-executable TryExec program, or is otherwise not capable of being used\n", path);
                g_hash_table_remove (gdm_available_sessions_map, id);
                goto out;
        }

        session = g_new0 (GdmSessionFile, 1);

        session->id = g_strdup (id);
        session->path = g_strdup (path);

        session->translated_name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL, NULL);
        session->translated_comment = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "Comment", NULL, NULL);

        g_hash_table_insert (gdm_available_sessions_map,
                             g_strdup (id),
                             session);
 out:
        g_key_file_free (key_file);
}

static gboolean
remove_duplicate_sessions (gpointer key,
                           gpointer value,
                           gpointer user_data)
{
        gboolean already_known;
        GHashTable *names_seen_before;
        GdmSessionFile *session;

        names_seen_before = (GHashTable *) user_data;
        session = (GdmSessionFile *) value;
        already_known = !g_hash_table_add (names_seen_before, session->translated_name);

        if (already_known)
                g_debug ("GdmSession: Removing %s (%s) as we already have a session by this name",
                         session->id,
                         session->path);

        return already_known;
}

static void
collect_sessions_from_directory (const char *dirname)
{
        GDir       *dir;
        const char *filename;

        gboolean is_x11 = g_getenv ("WAYLAND_DISPLAY") == NULL &&
                          g_getenv ("RUNNING_UNDER_GDM") != NULL;
        gboolean is_wayland = g_getenv ("WAYLAND_DISPLAY") != NULL &&
                              g_getenv ("RUNNING_UNDER_GDM") != NULL;

        /* FIXME: add file monitor to directory */

        dir = g_dir_open (dirname, 0, NULL);
        if (dir == NULL) {
                return;
        }

        while ((filename = g_dir_read_name (dir))) {
                char *id;
                char *full_path;

                if (! g_str_has_suffix (filename, ".desktop")) {
                        continue;
                }

                if (is_wayland) {
                        if (g_str_has_suffix (filename, "-wayland.desktop")) {
                                g_autofree char *base_name = g_strndup (filename, strlen (filename) - strlen ("-wayland.desktop"));
                                g_autofree char *other_name = g_strconcat (base_name, ".desktop", NULL);
                                g_autofree char *other_path = g_build_filename (dirname, other_name, NULL);

                                if (g_file_test (other_path, G_FILE_TEST_EXISTS)) {
                                        g_debug ("Running under Wayland, ignoring %s", filename);
                                        continue;
                                }
                        } else {
                                g_autofree char *base_name = g_strndup (filename, strlen (filename) - strlen (".desktop"));
                                g_autofree char *other_name = g_strdup_printf ("%s-xorg.desktop", base_name);
                                g_autofree char *other_path = g_build_filename (dirname, other_name, NULL);

                                if (g_file_test (other_path, G_FILE_TEST_EXISTS)) {
                                        g_debug ("Running under Wayland, ignoring %s", filename);
                                        continue;
                                }
                        }
                } else if (is_x11) {
                        if (g_str_has_suffix (filename, "-xorg.desktop")) {
                                g_autofree char *base_name = g_strndup (filename, strlen (filename) - strlen ("-xorg.desktop"));
                                g_autofree char *other_name = g_strconcat (base_name, ".desktop", NULL);
                                g_autofree char *other_path = g_build_filename (dirname, other_name, NULL);

                                if (g_file_test (other_path, G_FILE_TEST_EXISTS)) {
                                        g_debug ("Running under X11, ignoring %s", filename);
                                        continue;
                                }
                        } else {
                                g_autofree char *base_name = g_strndup (filename, strlen (filename) - strlen (".desktop"));
                                g_autofree char *other_name = g_strdup_printf ("%s-wayland.desktop", base_name);
                                g_autofree char *other_path = g_build_filename (dirname, other_name, NULL);

                                if (g_file_test (other_path, G_FILE_TEST_EXISTS)) {
                                        g_debug ("Running under X11, ignoring %s", filename);
                                        continue;
                                }
                        }
                }

                id = g_strndup (filename, strlen (filename) - strlen (".desktop"));

                full_path = g_build_filename (dirname, filename, NULL);

                load_session_file (id, full_path);

                g_free (id);
                g_free (full_path);
        }

        g_dir_close (dir);
}

static void
collect_sessions (void)
{
        g_autoptr(GHashTable) names_seen_before = NULL;
        g_autoptr(GPtrArray) dirs_search_array = NULL;
        gchar      *session_dir = NULL;
        int         i;
        const gchar *supported_session_types_env = NULL;
        g_auto (GStrv) supported_session_types = NULL;
        const gchar * const *system_data_dirs = g_get_system_data_dirs ();

        supported_session_types_env = g_getenv ("GDM_SUPPORTED_SESSION_TYPES");
        if (supported_session_types_env != NULL) {
                supported_session_types = g_strsplit (supported_session_types_env, ":", -1);
        }

        names_seen_before = g_hash_table_new (g_str_hash, g_str_equal);
        dirs_search_array = g_ptr_array_new_with_free_func (g_free);

#ifdef ENABLE_X11_SUPPORT
        const char *xorg_search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
        };

        if (!supported_session_types || g_strv_contains ((const char * const *) supported_session_types, "x11")) {
                for (i = 0; i < G_N_ELEMENTS (xorg_search_dirs); i++) {
                        g_ptr_array_add (dirs_search_array, g_strdup (xorg_search_dirs[i]));
                }

                for (i = 0; system_data_dirs[i]; i++) {
                        session_dir = g_build_filename (system_data_dirs[i], "xsessions", NULL);
                        g_ptr_array_add (dirs_search_array, session_dir);
                }
        }
#endif

#ifdef ENABLE_WAYLAND_SUPPORT
#ifdef ENABLE_USER_DISPLAY_SERVER
        if (!supported_session_types  || g_strv_contains ((const char * const *) supported_session_types, "wayland")) {
                for (i = 0; system_data_dirs[i]; i++) {
                        session_dir = g_build_filename (system_data_dirs[i], "wayland-sessions", NULL);
                        g_ptr_array_add (dirs_search_array, session_dir);
                }
        }
#endif
#endif

        if (gdm_available_sessions_map == NULL) {
                gdm_available_sessions_map = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                    g_free, (GDestroyNotify)gdm_session_file_free);
        }

        for (i = dirs_search_array->len - 1; i >= 0; i--) {
                collect_sessions_from_directory (g_ptr_array_index (dirs_search_array, i));
        }

        g_hash_table_foreach_remove (gdm_available_sessions_map,
                                     remove_duplicate_sessions,
                                     names_seen_before);
}

static gint
compare_session_ids (gconstpointer  a,
                     gconstpointer  b)
{
        GdmSessionFile *session_a, *session_b;
        session_a = (GdmSessionFile *) g_hash_table_lookup (gdm_available_sessions_map, a);
        session_b = (GdmSessionFile *) g_hash_table_lookup (gdm_available_sessions_map, b);

        if (session_a == NULL)
                return -1;

        if (session_b == NULL)
                return 1;

        return g_strcmp0 (session_a->translated_name, session_b->translated_name);
}

/**
 * gdm_get_session_ids:
 *
 * Reads /usr/share/xsessions and other relevant places for possible sessions
 * to log into and returns the complete list.
 *
 * Returns: (transfer full): a %NULL terminated list of session ids
 */
char **
gdm_get_session_ids (void)
{
        GHashTableIter iter;
        gpointer key, value;
        GPtrArray *array;

        if (!gdm_sessions_map_is_initialized) {
                collect_sessions ();

                gdm_sessions_map_is_initialized = TRUE;
        }

        array = g_ptr_array_new ();
        g_hash_table_iter_init (&iter, gdm_available_sessions_map);
        while (g_hash_table_iter_next (&iter, &key, &value)) {
                GdmSessionFile *session;

                session = (GdmSessionFile *) value;

                g_ptr_array_add (array, g_strdup (session->id));
        }
        g_ptr_array_add (array, NULL);

        g_ptr_array_sort (array, compare_session_ids);

        return (char **) g_ptr_array_free (array, FALSE);
}

/**
 * gdm_get_session_name_and_description:
 * @id: an id from gdm_get_session_ids()
 * @description: (out): optional returned session description
 *
 * Takes an xsession id and returns the name and comment about it.
 *
 * Returns: The session name if found, or %NULL otherwise
 */
char *
gdm_get_session_name_and_description (const char  *id,
                                      char       **description)
{
        GdmSessionFile *session;
        char *name;

        if (!gdm_sessions_map_is_initialized) {
                collect_sessions ();

                gdm_sessions_map_is_initialized = TRUE;
        }

        session = (GdmSessionFile *) g_hash_table_lookup (gdm_available_sessions_map,
                                                          id);

        if (session == NULL) {
                return NULL;
        }

        name = g_strdup (session->translated_name);

        if (description != NULL) {
                *description = g_strdup (session->translated_comment);
        }

        return name;
}
