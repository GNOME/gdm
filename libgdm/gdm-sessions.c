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

/* adapted from gnome-menus desktop-entries.c */
static gboolean
key_file_is_relevant (GKeyFile     *key_file)
{
        GError    *error;
        gboolean   no_display;
        gboolean   hidden;
        gboolean   tryexec_failed;
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

        if (no_display || hidden || tryexec_failed) {
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
                g_debug ("\"%s\" is hidden or contains non-executable TryExec program\n", path);
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

                if (is_x11 && g_str_has_suffix (filename, "-xorg.desktop")) {
                        char *base_name = g_strndup (filename, strlen (filename) - strlen ("-xorg.desktop"));
                        char *fallback_name = g_strconcat (base_name, ".desktop", NULL);
                        g_free (base_name);
                        char *fallback_path = g_build_filename (dirname, fallback_name, NULL);
                        g_free (fallback_name);
                        if (g_file_test (fallback_path, G_FILE_TEST_EXISTS)) {
                                g_free (fallback_path);
                                g_debug ("Running under X11, ignoring %s", filename);
                                continue;
                        }
                        g_free (fallback_path);
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
        g_autoptr(GPtrArray) xorg_search_array = NULL;
        g_autoptr(GPtrArray) wayland_search_array = NULL;
        gchar      *session_dir = NULL;
        int         i;
        const char *xorg_search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
        };

        names_seen_before = g_hash_table_new (g_str_hash, g_str_equal);
        xorg_search_array = g_ptr_array_new_with_free_func (g_free);

        const gchar * const *system_data_dirs = g_get_system_data_dirs ();

        for (i = 0; system_data_dirs[i]; i++) {
                session_dir = g_build_filename (system_data_dirs[i], "xsessions", NULL);
                g_ptr_array_add (xorg_search_array, session_dir);
        }

        for (i = 0; i < G_N_ELEMENTS (xorg_search_dirs); i++) {
                g_ptr_array_add (xorg_search_array, g_strdup (xorg_search_dirs[i]));
        }

#ifdef ENABLE_WAYLAND_SUPPORT
        const char *wayland_search_dirs[] = {
                DATADIR "/wayland-sessions/",
        };

        wayland_search_array = g_ptr_array_new_with_free_func (g_free);

        for (i = 0; system_data_dirs[i]; i++) {
                session_dir = g_build_filename (system_data_dirs[i], "wayland-sessions", NULL);
                g_ptr_array_add (wayland_search_array, session_dir);
        }

        for (i = 0; i < G_N_ELEMENTS (wayland_search_dirs); i++) {
                g_ptr_array_add (wayland_search_array, g_strdup (wayland_search_dirs[i]));
        }
#endif

        if (gdm_available_sessions_map == NULL) {
                gdm_available_sessions_map = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                    g_free, (GDestroyNotify)gdm_session_file_free);
        }

        for (i = 0; i < xorg_search_array->len; i++) {
                collect_sessions_from_directory (g_ptr_array_index (xorg_search_array, i));
        }

#ifdef ENABLE_WAYLAND_SUPPORT
#ifdef ENABLE_USER_DISPLAY_SERVER
        if (g_getenv ("WAYLAND_DISPLAY") == NULL && g_getenv ("RUNNING_UNDER_GDM") != NULL) {
                goto out;
        }
#endif

        for (i = 0; i < wayland_search_array->len; i++) {
                collect_sessions_from_directory (g_ptr_array_index (wayland_search_array, i));
        }
#endif

out:
        g_hash_table_foreach_remove (gdm_available_sessions_map,
                                     remove_duplicate_sessions,
                                     names_seen_before);
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
