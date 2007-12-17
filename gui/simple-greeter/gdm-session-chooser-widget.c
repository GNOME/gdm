/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007 Ray Strode <rstrode@redhat.com>
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
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>

#include "gdm-session-chooser-widget.h"
#include "gdm-chooser-widget.h"

enum {
        DESKTOP_ENTRY_NO_DISPLAY     = 1 << 0,
        DESKTOP_ENTRY_HIDDEN         = 1 << 1,
        DESKTOP_ENTRY_SHOW_IN_GNOME  = 1 << 2,
        DESKTOP_ENTRY_TRYEXEC_FAILED = 1 << 3
};

#define GDM_SESSION_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SESSION_CHOOSER_WIDGET, GdmSessionChooserWidgetPrivate))

typedef struct _GdmChooserSession {
        char    *filename;
        char    *path;
        char    *translated_name;
        char    *translated_comment;
        guint    flags;
} GdmChooserSession;

struct GdmSessionChooserWidgetPrivate
{
        GHashTable         *available_sessions;
};

enum {
        PROP_0,
};

static void     gdm_session_chooser_widget_class_init  (GdmSessionChooserWidgetClass *klass);
static void     gdm_session_chooser_widget_init        (GdmSessionChooserWidget      *session_chooser_widget);
static void     gdm_session_chooser_widget_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmSessionChooserWidget, gdm_session_chooser_widget, GDM_TYPE_CHOOSER_WIDGET)

static void
chooser_session_free (GdmChooserSession *session)
{
        if (session == NULL) {
                return;
        }

        g_free (session->filename);
        g_free (session->path);
        g_free (session->translated_name);
        g_free (session->translated_comment);

        g_free (session);
}

char *
gdm_session_chooser_widget_get_current_session_name (GdmSessionChooserWidget *widget)
{
        g_return_val_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (widget), NULL);
        return gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (widget));
}

void
gdm_session_chooser_widget_set_current_session_name (GdmSessionChooserWidget *widget,
                                                     const char              *name)
{
        g_return_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_active_item (GDM_CHOOSER_WIDGET (widget),
                                            name);
}

void
gdm_session_chooser_widget_set_show_only_chosen (GdmSessionChooserWidget *widget,
                                                 gboolean                 show_only)
{
        g_return_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_hide_inactive_items (GDM_CHOOSER_WIDGET (widget),
                                                    show_only);
}

static void
gdm_session_chooser_widget_set_property (GObject        *object,
                                          guint           prop_id,
                                          const GValue   *value,
                                          GParamSpec     *pspec)
{
        GdmSessionChooserWidget *self;

        self = GDM_SESSION_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_session_chooser_widget_get_property (GObject        *object,
                                          guint           prop_id,
                                          GValue         *value,
                                          GParamSpec     *pspec)
{
        GdmSessionChooserWidget *self;

        self = GDM_SESSION_CHOOSER_WIDGET (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
gdm_session_chooser_widget_constructor (GType                  type,
                                         guint                  n_construct_properties,
                                         GObjectConstructParam *construct_properties)
{
        GdmSessionChooserWidget      *session_chooser_widget;
        GdmSessionChooserWidgetClass *klass;

        klass = GDM_SESSION_CHOOSER_WIDGET_CLASS (g_type_class_peek (GDM_TYPE_SESSION_CHOOSER_WIDGET));

        session_chooser_widget = GDM_SESSION_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_session_chooser_widget_parent_class)->constructor (type,
                                                                                                                                       n_construct_properties,
                                                                                                                                       construct_properties));

        return G_OBJECT (session_chooser_widget);
}

static void
gdm_session_chooser_widget_dispose (GObject *object)
{
        GdmSessionChooserWidget *widget;

        widget = GDM_SESSION_CHOOSER_WIDGET (object);

        if (widget->priv->available_sessions != NULL) {
                g_hash_table_destroy (widget->priv->available_sessions);
                widget->priv->available_sessions = NULL;
        }

        G_OBJECT_CLASS (gdm_session_chooser_widget_parent_class)->dispose (object);
}

static void
gdm_session_chooser_widget_class_init (GdmSessionChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_session_chooser_widget_get_property;
        object_class->set_property = gdm_session_chooser_widget_set_property;
        object_class->constructor = gdm_session_chooser_widget_constructor;
        object_class->dispose = gdm_session_chooser_widget_dispose;
        object_class->finalize = gdm_session_chooser_widget_finalize;

        g_type_class_add_private (klass, sizeof (GdmSessionChooserWidgetPrivate));
}

/* adapted from gnome-menus desktop-entries.c */
static guint
get_flags_from_key_file (GKeyFile     *key_file,
                         const char   *desktop_entry_group)
{
        GError    *error;
        gboolean   no_display;
        gboolean   hidden;
        gboolean   tryexec_failed;
        char      *tryexec;
        guint      flags;

        error = NULL;
        no_display = g_key_file_get_boolean (key_file,
                                             desktop_entry_group,
                                             "NoDisplay",
                                             &error);
        if (error) {
                no_display = FALSE;
                g_error_free (error);
        }

        error = NULL;
        hidden = g_key_file_get_boolean (key_file,
                                         desktop_entry_group,
                                         "Hidden",
                                         &error);
        if (error) {
                hidden = FALSE;
                g_error_free (error);
        }

        tryexec_failed = FALSE;
        tryexec = g_key_file_get_string (key_file,
                                         desktop_entry_group,
                                         "TryExec",
                                         NULL);
        if (tryexec) {
                char *path;

                path = g_find_program_in_path (g_strstrip (tryexec));

                tryexec_failed = (path == NULL);

                g_free (path);
                g_free (tryexec);
        }

        flags = 0;
        if (no_display)
                flags |= DESKTOP_ENTRY_NO_DISPLAY;
        if (hidden)
                flags |= DESKTOP_ENTRY_HIDDEN;
        if (tryexec_failed)
                flags |= DESKTOP_ENTRY_TRYEXEC_FAILED;

        return flags;
}

static void
load_session_file (GdmSessionChooserWidget *widget,
                   const char              *name,
                   const char              *path)
{
        GKeyFile          *key_file;
        GError            *error;
        gboolean           res;
        GdmChooserSession *session;

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

        session = g_new0 (GdmChooserSession, 1);

        session->filename = g_strdup (name);
        session->path = g_strdup (path);
        session->flags = get_flags_from_key_file (key_file, G_KEY_FILE_DESKTOP_GROUP);

        session->translated_name = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "Name", NULL, NULL);
        session->translated_comment = g_key_file_get_locale_string (key_file, G_KEY_FILE_DESKTOP_GROUP, "Comment", NULL, NULL);

        g_hash_table_insert (widget->priv->available_sessions,
                             g_strdup (name),
                             session);
 out:
        g_key_file_free (key_file);
}

static void
collect_sessions_from_directory (GdmSessionChooserWidget *widget,
                                 const char              *dirname)
{
        GDir       *dir;
        const char *filename;

        /* FIXME: add file monitor to directory */

        dir = g_dir_open (dirname, 0, NULL);
        if (dir == NULL) {
                return;
        }

        while ((filename = g_dir_read_name (dir))) {
                char *full_path;

                if (! g_str_has_suffix (filename, ".desktop")) {
                        continue;
                }

                full_path = g_build_filename (dirname, filename, NULL);

                load_session_file (widget, filename, full_path);

                g_free (full_path);
        }

        g_dir_close (dir);
}

static void
collect_sessions_from_directories (GdmSessionChooserWidget *widget)
{
        int         i;
        const char *search_dirs[] = {
                "/etc/X11/sessions/",
                DMCONFDIR "/Sessions/",
                DATADIR "/gdm/BuiltInSessions/",
                DATADIR "/xsessions/",
                NULL
        };

        for (i = 0; search_dirs [i] != NULL; i++) {
                collect_sessions_from_directory (widget, search_dirs [i]);
        }
}

static void
collect_sessions (GdmSessionChooserWidget *widget)
{
        collect_sessions_from_directories (widget);
}

static void
add_session (const char              *name,
             GdmChooserSession       *session,
             GdmSessionChooserWidget *widget)
{
        g_assert (name != NULL);
        g_assert (session != NULL);
        g_assert (GDM_IS_SESSION_CHOOSER_WIDGET (widget));

        if (session->flags & DESKTOP_ENTRY_NO_DISPLAY
            || session->flags & DESKTOP_ENTRY_HIDDEN
            || session->flags & DESKTOP_ENTRY_TRYEXEC_FAILED) {
                /* skip */
                g_debug ("Not adding session to list: %s", session->filename);
        }

        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget), name,
                                     NULL, session->translated_name,
                                     session->translated_comment, FALSE, FALSE);
}

static void
add_available_sessions (GdmSessionChooserWidget *widget)
{
        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_SESSION_CHOOSER_SESSION_PREVIOUS,
                                     NULL, _("Default"),
                                     _("Login with the same session as "
                                       "last time."),
                                     FALSE, TRUE);
        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_SESSION_CHOOSER_SESSION_DEFAULT,
                                     NULL, _("Legacy"),
                                     _("Login based on preset legacy configuration"),
                                     FALSE, TRUE);

        g_hash_table_foreach (widget->priv->available_sessions,
                              (GHFunc) add_session,
                              widget);
}

static void
gdm_session_chooser_widget_init (GdmSessionChooserWidget *widget)
{
        widget->priv = GDM_SESSION_CHOOSER_WIDGET_GET_PRIVATE (widget);

        gdm_chooser_widget_set_separator_position (GDM_CHOOSER_WIDGET (widget),
                                                   GDM_CHOOSER_WIDGET_POSITION_TOP);
        widget->priv->available_sessions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)chooser_session_free);

        collect_sessions (widget);

        add_available_sessions (widget);
}

static void
gdm_session_chooser_widget_finalize (GObject *object)
{
        GdmSessionChooserWidget *session_chooser_widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (object));

        session_chooser_widget = GDM_SESSION_CHOOSER_WIDGET (object);

        g_return_if_fail (session_chooser_widget->priv != NULL);

        G_OBJECT_CLASS (gdm_session_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_session_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SESSION_CHOOSER_WIDGET, 
                               "inactive-text", _("_Sessions:"),
                               "active-text", _("_Session:"),
                               NULL);

        return GTK_WIDGET (object);
}
