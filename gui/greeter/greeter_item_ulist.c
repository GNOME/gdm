/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <gtk/gtk.h>
#include <libgnome/libgnome.h>
#include <librsvg/rsvg.h>
#include "vicious.h"

#include "gdm.h"
#include "gdmcommon.h"
#include "gdmuser.h"
#include "greeter.h"
#include "greeter_item_ulist.h"
#include "greeter_parser.h"
#include "greeter_configuration.h"

static GList *users = NULL;
static GList *users_string = NULL;
static GdkPixbuf *defface;

static GtkWidget *pam_entry = NULL;
static GtkWidget *user_list = NULL;

static gboolean selecting_user = FALSE;

enum
{
  GREETER_ULIST_ICON_COLUMN = 0,
  GREETER_ULIST_LABEL_COLUMN,
  GREETER_ULIST_LOGIN_COLUMN
};

static void 
gdm_greeter_users_init (void)
{
	gint          size_of_users = 0;
	time_t        time_started;

	defface = gdm_common_get_face (NULL,
				       GdmDefaultFace,
				       GdmIconMaxWidth,
				       GdmIconMaxHeight);
	if (! defface) {
		syslog (LOG_WARNING,
			_("Can't open DefaultImage: %s!"),
			GdmDefaultFace);
	}

	gdm_users_init (&users, &users_string, NULL, defface,
			&size_of_users, GDM_IS_LOCAL, !DOING_GDM_DEVELOPMENT);
}

static void
greeter_populate_user_list (GtkTreeModel *tm)
{
  GList *li;

  for (li = users; li != NULL; li = li->next)
    {
      GdmUser *usr = li->data;
      GtkTreeIter iter = {0};
      char *label;
      char *login, *gecos;

      login = g_markup_escape_text (usr->login, -1);
      gecos = g_markup_escape_text (usr->gecos, -1);

      if (usr->gecos && strcmp (usr->gecos, "") != 0) {
	      label = g_strdup_printf ("<b>%s</b>\n    %s",
				       gecos,
				       login);
      } else {
	      label = g_strdup_printf ("<b>%s</b>\n%s",
				       login,
				       gecos);
      }

      g_free (login);
      g_free (gecos);
      gtk_list_store_append (GTK_LIST_STORE (tm), &iter);
      gtk_list_store_set (GTK_LIST_STORE (tm), &iter,
			  GREETER_ULIST_ICON_COLUMN, usr->picture,
			  GREETER_ULIST_LOGIN_COLUMN, usr->login,
			  GREETER_ULIST_LABEL_COLUMN, label,
			  -1);
      g_free (label);
    }

}

static void
user_selected (GtkTreeSelection *selection, gpointer data)
{
  GtkTreeModel *tm = NULL;
  GtkTreeIter iter = {0};

  if (gtk_tree_selection_get_selected (selection, &tm, &iter))
    {
      char *login;
      gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			  &login, -1);
      if (login != NULL)
        {
          GreeterItemInfo *pamlabel;

          if (selecting_user && greeter_probably_login_prompt)
            {
               gtk_entry_set_text (GTK_ENTRY (pam_entry), login);
            }
          pamlabel = greeter_lookup_id ("pam-message");
          if (selecting_user && pamlabel != NULL)
            {
               printf ("%c%c%c%s\n", STX, BEL,
                 GDM_INTERRUPT_SELECT_USER, login);
               fflush (stdout);
            }
	}
    }
}

static void
browser_change_focus (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    gtk_widget_grab_focus (pam_entry);
}

static void
greeter_generate_userlist (GtkWidget *tv)
{
	GtkTreeModel *tm;
	GtkTreeViewColumn *column_one, *column_two;
	GtkTreeSelection *selection;
	GreeterItemInfo *info;
	GList *list, *li;

	gdm_greeter_users_init ();
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tv),
					   FALSE);
	selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tv));
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	if (users != NULL)
	{
		g_signal_connect (selection, "changed",
			G_CALLBACK (user_selected),
			NULL);

		g_signal_connect (GTK_TREE_VIEW (tv), "button_release_event",
			G_CALLBACK (browser_change_focus),
			NULL);

		tm = (GtkTreeModel *)gtk_list_store_new (3,
					       GDK_TYPE_PIXBUF,
					       G_TYPE_STRING,
					       G_TYPE_STRING);
		gtk_tree_view_set_model (GTK_TREE_VIEW (tv), tm);
		column_one = gtk_tree_view_column_new_with_attributes
		             (_("Icon"),
			      gtk_cell_renderer_pixbuf_new (),
			      "pixbuf", GREETER_ULIST_ICON_COLUMN,
			      NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column_one);

		column_two = gtk_tree_view_column_new_with_attributes
		             (_("Username"),
			      gtk_cell_renderer_text_new (),
			      "markup", GREETER_ULIST_LABEL_COLUMN,
			      NULL);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column_two);

		greeter_populate_user_list (tm);

		info = greeter_lookup_id ("userlist");

		list = gtk_tree_view_column_get_cell_renderers (column_one);
		for (li = list; li != NULL; li = li->next) {
			GtkObject *cell = li->data;

			if (info->data.list.icon_color != NULL)
				g_object_set (cell, "cell-background",
					info->data.list.icon_color, NULL);
		}

		list = gtk_tree_view_column_get_cell_renderers (column_two);
		for (li = list; li != NULL; li = li->next) {
			GtkObject *cell = li->data;

			if (info->data.list.label_color != NULL) 
				g_object_set (cell, "background",
					info->data.list.label_color, NULL);
		}
	}
}

static inline void
force_no_tree_separators (GtkWidget *widget)
{
	gboolean first_time = TRUE;

	if (first_time) {
		gtk_rc_parse_string ("\n"
				     "	 style \"gdm-userlist-treeview-style\"\n"
				     "	 {\n"
				     "	    GtkTreeView::horizontal-separator=0\n"
				     "	    GtkTreeView::vertical-separator=0\n"
				     "	 }\n"
				     "\n"
				     "	  widget \"*.gdm-userlist-treeview\" style \"gdm-userlist-treeview-style\"\n"
				     "\n");
		first_time = FALSE;
	}

	gtk_widget_set_name (widget, "gdm-userlist-treeview");
}

gboolean
greeter_item_ulist_setup (void)
{
  GreeterItemInfo *info;

  info = greeter_lookup_id ("user-pw-entry");
  if (info && info->item &&
      GNOME_IS_CANVAS_WIDGET (info->item) &&
      GTK_IS_ENTRY (GNOME_CANVAS_WIDGET (info->item)->widget))
    {
      pam_entry = GNOME_CANVAS_WIDGET (info->item)->widget;
    }
  info = greeter_lookup_id ("userlist");
  if (info && info->item &&
      GNOME_IS_CANVAS_WIDGET (info->item))
    {
      GtkWidget *sw = GNOME_CANVAS_WIDGET (info->item)->widget;
      if (GTK_IS_SCROLLED_WINDOW (sw) && 
	  GTK_IS_TREE_VIEW (GTK_BIN (sw)->child))
        {
	  GtkRequisition req;
	  gdouble        height;

          user_list = GTK_BIN (sw)->child;

	  force_no_tree_separators (user_list);

          greeter_generate_userlist (user_list);
	  if ( ! DOING_GDM_DEVELOPMENT)
            greeter_item_ulist_disable ();

         /* Reset size of the widget canvas item so it is the same
          * size as the userlist.  This avoids the ugly white background
          * displayed below the Face Browser when the list isn't as large
          * as the rectangle defined in the GDM theme file.
          */
	  gtk_widget_size_request (user_list, &req);
	  g_object_get (info->item, "height", &height, NULL);

	  if (req.height < height)
		  g_object_set (info->item, "height", (double)req.height, NULL);
        }
    }
  return TRUE;
}

void
greeter_item_ulist_enable (void)
{
  selecting_user = TRUE;
  if (user_list != NULL)
    gtk_widget_set_sensitive (user_list, TRUE);
}

void
greeter_item_ulist_disable (void)
{
  selecting_user = FALSE;
  if (user_list != NULL)
    gtk_widget_set_sensitive (user_list, FALSE);
}

void
greeter_item_ulist_set_user (const char *user)
{
  gboolean old_selecting_user = selecting_user;
  GtkTreeSelection *selection;
  GtkTreeIter iter = {0};
  GtkTreeModel *tm = NULL;

  if (user_list == NULL)
    return;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (user_list));
  gtk_tree_selection_unselect_all (selection);

  if (ve_string_empty (user))
    return;

  /* Make sure we don't set the pam_entry and pam label stuff,
     this is programatic selection, not user selection */
  selecting_user = FALSE;

  tm = gtk_tree_view_get_model (GTK_TREE_VIEW (user_list));

  if (gtk_tree_model_get_iter_first (tm, &iter))
    {
      do
        {
          char *login;
	  gtk_tree_model_get (tm, &iter, GREETER_ULIST_LOGIN_COLUMN,
			      &login, -1);
	  if (login != NULL && strcmp (user, login) == 0)
	    {
	      GtkTreePath *path = gtk_tree_model_get_path (tm, &iter);
	      gtk_tree_selection_select_iter (selection, &iter);
	      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (user_list),
					    path, NULL,
					    FALSE, 0.0, 0.0);
	      gtk_tree_path_free (path);
	      break;
	    }
	  
        }
      while (gtk_tree_model_iter_next (tm, &iter));
    }
  selecting_user = old_selecting_user;
}
