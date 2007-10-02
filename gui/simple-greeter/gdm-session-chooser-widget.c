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
        GtkWidget          *treeview;

        GHashTable         *available_sessions;
        char               *current_session;
};

enum {
        PROP_0,
};

enum {
        SESSION_ACTIVATED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_session_chooser_widget_class_init  (GdmSessionChooserWidgetClass *klass);
static void     gdm_session_chooser_widget_init        (GdmSessionChooserWidget      *session_chooser_widget);
static void     gdm_session_chooser_widget_finalize    (GObject                       *object);

G_DEFINE_TYPE (GdmSessionChooserWidget, gdm_session_chooser_widget, GTK_TYPE_VBOX)

enum {
        CHOOSER_LIST_NAME_COLUMN = 0,
        CHOOSER_LIST_COMMENT_COLUMN,
        CHOOSER_LIST_ID_COLUMN
};

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
        char *session_name;

        g_return_val_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (widget), NULL);

        session_name = NULL;
        if (widget->priv->current_session != NULL) {
                session_name = g_strdup (widget->priv->current_session);
        }

        return session_name;
}

static void
select_name (GdmSessionChooserWidget *widget,
             const char               *name)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;
        GtkTreePath  *path;

        path = NULL;

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget->priv->treeview));

        if (name != NULL && gtk_tree_model_get_iter_first (model, &iter)) {

                do {
                        GdmChooserSession *session;
                        char              *id;
                        gboolean          found;

                        session = NULL;
                        id = NULL;
                        gtk_tree_model_get (model,
                                            &iter,
                                            CHOOSER_LIST_ID_COLUMN, &id,
                                            -1);
                        if (id != NULL) {
                                session = g_hash_table_lookup (widget->priv->available_sessions, id);
                                g_free (id);
                        }

                        found = (session != NULL
                                 && session->filename != NULL
                                 && strcmp (session->filename, name) == 0);

                        if (found) {
                                path = gtk_tree_model_get_path (model, &iter);
                                break;
                        }

                } while (gtk_tree_model_iter_next (model, &iter));
        }

        if (path != NULL) {
                gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (widget->priv->treeview),
                                              path,
                                              gtk_tree_view_get_column (GTK_TREE_VIEW (widget->priv->treeview), 0),
                                              TRUE, 0.5, 0.0);
                gtk_tree_view_set_cursor (GTK_TREE_VIEW (widget->priv->treeview),
                                          path,
                                          NULL,
                                          FALSE);

                gtk_tree_path_free (path);
        }
}

void
gdm_session_chooser_widget_set_current_session_name (GdmSessionChooserWidget *widget,
                                                     const char              *name)
{
        GtkTreeSelection *selection;

        g_return_if_fail (GDM_IS_SESSION_CHOOSER_WIDGET (widget));

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));

        if (name == NULL) {
                gtk_tree_selection_unselect_all (selection);
        } else {
                select_name (widget, name);
        }
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

        g_free (widget->priv->current_session);
        widget->priv->current_session = NULL;

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

        signals [SESSION_ACTIVATED] = g_signal_new ("session-activated",
                                                     G_TYPE_FROM_CLASS (object_class),
                                                     G_SIGNAL_RUN_LAST,
                                                     G_STRUCT_OFFSET (GdmSessionChooserWidgetClass, session_activated),
                                                     NULL,
                                                     NULL,
                                                     g_cclosure_marshal_VOID__VOID,
                                                     G_TYPE_NONE,
                                                     0);

        g_type_class_add_private (klass, sizeof (GdmSessionChooserWidgetPrivate));
}

static void
on_session_selected (GtkTreeSelection        *selection,
                     GdmSessionChooserWidget *widget)
{
        GtkTreeModel      *model = NULL;
        GtkTreeIter        iter = {0};
        char              *id;

        id = NULL;

        if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
                gtk_tree_model_get (model, &iter, CHOOSER_LIST_ID_COLUMN, &id, -1);
        }

        g_free (widget->priv->current_session);
        widget->priv->current_session = g_strdup (id);

        g_free (id);
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
on_row_activated (GtkTreeView          *tree_view,
                  GtkTreePath          *tree_path,
                  GtkTreeViewColumn    *tree_column,
                  GdmSessionChooserWidget *widget)
{
        g_signal_emit (widget, signals[SESSION_ACTIVATED], 0);
}

static void
add_session_to_model (const char              *name,
                      GdmChooserSession       *session,
                      GdmSessionChooserWidget *widget)
{
        GtkTreeModel *model;
        GtkTreeIter   iter;

        if (session->flags & DESKTOP_ENTRY_NO_DISPLAY
            || session->flags & DESKTOP_ENTRY_HIDDEN
            || session->flags & DESKTOP_ENTRY_TRYEXEC_FAILED) {
                /* skip */
                g_debug ("Not adding session to list: %s", session->filename);
        }

        model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget->priv->treeview));

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model),
                            &iter,
                            CHOOSER_LIST_NAME_COLUMN, session->translated_name,
                            CHOOSER_LIST_COMMENT_COLUMN, session->translated_comment,
                            CHOOSER_LIST_ID_COLUMN, name,
                            -1);
}

static void
populate_model (GdmSessionChooserWidget *widget,
                GtkTreeModel            *model)
{
        GtkTreeIter iter;

        /* Add some fake entries */
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_NAME_COLUMN, _("Previous Session"),
                            CHOOSER_LIST_ID_COLUMN, "__previous-session",
                            -1);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_NAME_COLUMN, _("System Default"),
                            CHOOSER_LIST_ID_COLUMN, "__default-session",
                            -1);

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            CHOOSER_LIST_NAME_COLUMN, NULL,
                            CHOOSER_LIST_ID_COLUMN, "__separator",
                            -1);

        g_hash_table_foreach (widget->priv->available_sessions,
                              (GHFunc)add_session_to_model,
                              widget);
}

static gboolean
separator_func (GtkTreeModel *model,
                GtkTreeIter  *iter,
                gpointer      data)
{
        int   column = GPOINTER_TO_INT (data);
        char *text;

        gtk_tree_model_get (model, iter, column, &text, -1);

        if (text != NULL && strcmp (text, "__separator") == 0) {
                return TRUE;
        }

        g_free (text);

        return FALSE;
}

static int
compare_session_names (char *name_a,
                       char *name_b,
                       char *id_a,
                       char *id_b)
{

        if (id_a == NULL) {
                return 1;
        } else if (id_b == NULL) {
                return -1;
        }

        if (strcmp (id_a, "__previous-session") == 0) {
                return -1;
        } else if (strcmp (id_b, "__previous-session") == 0) {
                return 1;
        } else if (strcmp (id_a, "__default-session") == 0) {
                return -1;
        } else if (strcmp (id_b, "__default-session") == 0) {
                return 1;
        } else if (strcmp (id_a, "__separator") == 0) {
                return -1;
        } else if (strcmp (id_b, "__separator") == 0) {
                return 1;
        }

        if (name_a == NULL) {
                return 1;
        } else if (name_b == NULL) {
                return -1;
        }

        return g_utf8_collate (name_a, name_b);
}

static int
compare_session  (GtkTreeModel *model,
                  GtkTreeIter  *a,
                  GtkTreeIter  *b,
                  gpointer      user_data)
{
        char *name_a;
        char *name_b;
        char *id_a;
        char *id_b;
        int   result;

        gtk_tree_model_get (model, a, CHOOSER_LIST_NAME_COLUMN, &name_a, -1);
        gtk_tree_model_get (model, b, CHOOSER_LIST_NAME_COLUMN, &name_b, -1);
        gtk_tree_model_get (model, a, CHOOSER_LIST_ID_COLUMN, &id_a, -1);
        gtk_tree_model_get (model, b, CHOOSER_LIST_ID_COLUMN, &id_b, -1);

        result = compare_session_names (name_a, name_b, id_a, id_b);

        g_free (name_a);
        g_free (name_b);
        g_free (id_a);
        g_free (id_b);

        return result;
}

static void
gdm_session_chooser_widget_init (GdmSessionChooserWidget *widget)
{
        GtkWidget         *scrolled;
        GtkTreeSelection  *selection;
        GtkTreeViewColumn *column;
        GtkTreeModel      *model;

        widget->priv = GDM_SESSION_CHOOSER_WIDGET_GET_PRIVATE (widget);

        widget->priv->available_sessions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)chooser_session_free);

        scrolled = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled),
                                             GTK_SHADOW_IN);
        gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled),
                                        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_box_pack_start (GTK_BOX (widget), scrolled, TRUE, TRUE, 0);

        widget->priv->treeview = gtk_tree_view_new ();
        gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (widget->priv->treeview), FALSE);
        g_signal_connect (widget->priv->treeview,
                          "row-activated",
                          G_CALLBACK (on_row_activated),
                          widget);
        gtk_container_add (GTK_CONTAINER (scrolled), widget->priv->treeview);

        selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget->priv->treeview));
        gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
        g_signal_connect (selection, "changed",
                          G_CALLBACK (on_session_selected),
                          widget);

        model = (GtkTreeModel *)gtk_list_store_new (3,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING,
                                                    G_TYPE_STRING);
        gtk_tree_view_set_model (GTK_TREE_VIEW (widget->priv->treeview), model);

        column = gtk_tree_view_column_new_with_attributes ("Session",
                                                           gtk_cell_renderer_text_new (),
                                                           "text", CHOOSER_LIST_NAME_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);

        column = gtk_tree_view_column_new_with_attributes ("Comment",
                                                           gtk_cell_renderer_text_new (),
                                                           "text", CHOOSER_LIST_COMMENT_COLUMN,
                                                           NULL);
        gtk_tree_view_append_column (GTK_TREE_VIEW (widget->priv->treeview), column);

        gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (model),
                                         CHOOSER_LIST_NAME_COLUMN,
                                         compare_session,
                                         NULL, NULL);

        gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (model),
                                              CHOOSER_LIST_NAME_COLUMN,
                                              GTK_SORT_ASCENDING);

        gtk_tree_view_set_row_separator_func (GTK_TREE_VIEW (widget->priv->treeview),
                                              separator_func,
                                              GINT_TO_POINTER (CHOOSER_LIST_ID_COLUMN),
                                              NULL);

        collect_sessions (widget);

        populate_model (widget, model);
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
                               NULL);

        return GTK_WIDGET (object);
}
