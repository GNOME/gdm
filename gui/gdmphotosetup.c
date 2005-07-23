/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *    GDMphotosetup - graphical .gnome2/photo setup program for users
 *
 *    Copyright (C) 2001 Queen of England
 *    Copyright (C) 2005 William Jon McCann <mccann@jhu.edu>
 *    
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *   
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *   
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 */

#include "config.h"
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <libgnome/libgnome.h> /* for gnome_config */
#include <libgnomeui/libgnomeui.h>

#include <viciousui.h>

#include "gdm.h"
#include "gdmcommon.h"

static GladeXML *xml;
static char	*photofile;
static char	*facedir;
static int	 max_width, max_height;

static void
dialog_response (GtkWidget *dialog,
		 int	    res,
		 gpointer   data)
{
	if (res == GTK_RESPONSE_HELP)
		return;

	gtk_main_quit ();
}

static GdkPixbuf *
scale_pixbuf (GdkPixbuf *pixbuf)
{
	float	   scale_factor_x = 1.0;
	float	   scale_factor_y = 1.0;
	float	   scale_factor = 1.0;
	GdkPixbuf *scaled = NULL;

	/* Determine which dimension requires the smallest scale. */
	if (gdk_pixbuf_get_width (pixbuf) > max_width)
		scale_factor_x = (float) max_width /
			(float) gdk_pixbuf_get_width (pixbuf);

	if (gdk_pixbuf_get_height (pixbuf) > max_height)
		scale_factor_y = (float) max_height /
			(float) gdk_pixbuf_get_height (pixbuf);

	if (scale_factor_x > scale_factor_y)
		scale_factor = scale_factor_y;
	else
		scale_factor = scale_factor_x;

	/* Only scale if it needs to be scaled smaller */
	if (scale_factor >= 1.0) {
		scaled = g_object_ref (pixbuf);
	} else {
		int scale_x = (int) (gdk_pixbuf_get_width (pixbuf) *
				     scale_factor);
		int scale_y = (int) (gdk_pixbuf_get_height (pixbuf) *
				     scale_factor);

		/* Scale bigger dimension to max icon height/width */
		scaled = gdk_pixbuf_scale_simple (pixbuf,
						  scale_x,
						  scale_y,
						  GDK_INTERP_BILINEAR);
	}

	return scaled;
}

static void
set_preview_pixbuf (GtkImage  *image,
		    GdkPixbuf *pixbuf)
{
	GdkPixbuf *preview_pixbuf;

	preview_pixbuf = scale_pixbuf (pixbuf);
	gtk_image_set_from_pixbuf (GTK_IMAGE (image), preview_pixbuf);

	gdk_pixbuf_unref (preview_pixbuf);
}

static void
update_preview_cb (GtkFileChooser *file_chooser,
		   GtkImage	  *image)
{
	GdkPixbuf *pixbuf;
	gboolean   have_preview;
	char	  *filename;

	g_return_if_fail (GTK_IS_FILE_CHOOSER (file_chooser));
	g_return_if_fail (GTK_IS_IMAGE (image));

	filename = gtk_file_chooser_get_preview_filename (file_chooser);

	if (! filename) {
		gtk_file_chooser_set_preview_widget_active (file_chooser, FALSE);
		return;
	}

	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	have_preview = (pixbuf != NULL);
	g_free (filename);

	if (pixbuf) {
		set_preview_pixbuf (image, pixbuf);
		gdk_pixbuf_unref (pixbuf);
	}

	gtk_file_chooser_set_preview_widget_active (file_chooser, have_preview);
}

static void
set_face_from_filename (const char *filename)
{
	GtkWidget *image;
	GdkPixbuf *pixbuf;
	GdkPixbuf *scaled;

	image = glade_xml_get_widget (xml, "face_image");

	pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
	scaled = scale_pixbuf (pixbuf);
	g_object_unref (pixbuf);
	pixbuf = scaled;

	if (! pixbuf)
		return;

	if (gdk_pixbuf_save (pixbuf, photofile, "png", NULL, NULL) != TRUE) {
		GtkWidget *d;
		char	  *tmp = g_filename_to_utf8 (photofile, -1, NULL, NULL, NULL);

		d = ve_hig_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       FALSE /* markup */,
				       _("Cannot open file"),
				       _("File %s cannot be opened for "
					 "writing\n"), tmp);

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);

		g_free (tmp);
	} else {
		/* Change to g_chmod after glib 2.8 release */
		chmod (photofile, 0644);
	}

	gtk_image_set_from_file (GTK_IMAGE (image), photofile);
}

static void
install_response (GtkWidget *file_dialog,
		  gint	     response,
		  gpointer   data)
{
	char *name = NULL;

	if (response == GTK_RESPONSE_ACCEPT) {
		name = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		set_face_from_filename (name);
	}

	g_free (name);

	gtk_widget_destroy (file_dialog);
}

static GtkWidget *
add_preview_widget (GtkWidget *widget)
{
	GtkWidget *vbox;
	GtkWidget *image;

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

	image = gtk_image_new ();
	gtk_widget_set_size_request (image, 128, 128);

	gtk_box_pack_start (GTK_BOX (vbox), image, FALSE, TRUE, 0);
	gtk_widget_show_all (vbox);

	gtk_file_chooser_set_preview_widget_active (GTK_FILE_CHOOSER (widget), FALSE);
	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (widget), vbox);

	g_signal_connect (widget, "update-preview",
			  G_CALLBACK (update_preview_cb), image);
}

static void
browse_button_cb (GtkWidget *widget, gpointer data)
{
	GtkWindow     *parent = GTK_WINDOW (data);
	GtkFileFilter *all_img_filter;
	GSList	      *formats;
	GSList	      *l;
	GtkWidget     *file_dialog;
	GSList	      *filters = NULL;
	GtkWidget     *vbox;

	file_dialog = gtk_file_chooser_dialog_new (_("Select Image"),
						   parent,
						   GTK_FILE_CHOOSER_ACTION_OPEN,
						   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						   GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
						   NULL);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_dialog), TRUE);

	if (facedir && g_file_test (facedir, G_FILE_TEST_IS_DIR)) {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
						     facedir);
		gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (file_dialog),
						      facedir, NULL);
	} else {
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
						     EXPANDED_DATADIR "/pixmaps");
		gtk_file_chooser_add_shortcut_folder (GTK_FILE_CHOOSER (file_dialog),
						      EXPANDED_DATADIR "/pixmaps", NULL);
	}

	all_img_filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (all_img_filter, _("All Images"));

	formats = gdk_pixbuf_get_formats ();

	/* Image filters */
	for (l = formats; l != NULL; l = l->next) {
		int		 i;
		char		*filter_name;
		char		*description, *extension;
		GdkPixbufFormat *format;
		GtkFileFilter	*filter;
		char	       **mime_types, **pattern, *tmp;

		filter = gtk_file_filter_new ();

		format = (GdkPixbufFormat*) l->data;
		description = gdk_pixbuf_format_get_description (format);
		extension = gdk_pixbuf_format_get_name (format);

		/* Filter name: First description then file extension, eg. "The PNG-Format (*.png)".*/
		filter_name = g_strdup_printf (_("%s (*.%s)"), description, extension);
		g_free (description);
		g_free (extension);

		gtk_file_filter_set_name (filter, filter_name);
		g_free (filter_name);

		mime_types = gdk_pixbuf_format_get_mime_types ((GdkPixbufFormat *) l->data);
		for (i = 0; mime_types[i] != NULL; i++) {
			gtk_file_filter_add_mime_type (filter, mime_types[i]);
			gtk_file_filter_add_mime_type (all_img_filter, mime_types[i]);
		}
		g_strfreev (mime_types);
 
		pattern = gdk_pixbuf_format_get_extensions ((GdkPixbufFormat *) l->data);
		for (i = 0; pattern[i] != NULL; i++) {
			tmp = g_strconcat ("*.", pattern[i], NULL);
			gtk_file_filter_add_pattern (filter, tmp);
			gtk_file_filter_add_pattern (all_img_filter, tmp);
			g_free (tmp);
		}
		g_strfreev (pattern);

		filters = g_slist_prepend (filters, filter);
	}
	g_slist_free (formats);

	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), all_img_filter);
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (file_dialog), all_img_filter);

	for (l = filters; l != NULL; l = l->next) {
		gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog),
					     GTK_FILE_FILTER (l->data));
	}
	g_slist_free (filters);

	add_preview_widget (file_dialog);

	g_signal_connect (G_OBJECT (file_dialog), "destroy",
			  G_CALLBACK (gtk_widget_destroyed), &file_dialog);
	g_signal_connect (G_OBJECT (file_dialog), "response",
			  G_CALLBACK (install_response), NULL);

	gtk_widget_show (file_dialog);
}

static void
maybe_migrate_old_config (void)
{
	char *name;

	/* Note, change access to g_access after GTK 2.8 is released */
	if (photofile && access (photofile, R_OK) == 0)
		return;

	/* if we don't have a face then look for old one */
	name = gnome_config_get_string ("/gdmphotosetup/last/picture");
	if (name && access (name, R_OK) != 0) {
		GdkPixbuf *pixbuf;

		pixbuf = gdk_pixbuf_new_from_file (name, NULL);
		if (pixbuf) {
			gdk_pixbuf_save (pixbuf, photofile, "png", NULL, NULL);
		}

	}

	g_free (name);
}

static GtkTreeModel *
create_model (void)
{
	GtkListStore *store;
  
	store = gtk_list_store_new (2,
				    GDK_TYPE_PIXBUF,
				    G_TYPE_STRING);

	return GTK_TREE_MODEL (store);
}

static void
fill_model (GtkTreeModel *model)
{
	GdkPixbuf    *pixbuf;
	int	      i;
	char	     *str, *str2;
	GtkTreeIter   iter;
	GtkListStore *store = GTK_LIST_STORE (model);
	GDir	     *dir;
	const char   *filename;

	dir = g_dir_open (facedir, 0, NULL);
	if (! dir)
		return;
  
	while ((filename = g_dir_read_name (dir)) != NULL) {
		char *path;

		path = g_build_filename (facedir, filename, NULL);
		pixbuf = gdk_pixbuf_new_from_file (path, NULL);
		if (! pixbuf)
			continue;

		gtk_list_store_prepend (store, &iter);

		gtk_list_store_set (store, &iter,
				    0, pixbuf,
				    1, path,
				    -1);
	}

	g_dir_close (dir);
}

static void
selection_foreach (GtkIconView *icon_view,
		   GtkTreePath *path,
		   gpointer	data)
{
	GtkTreeModel *model;
	GtkTreeIter   iter;
	char	     *filename;

	model = gtk_icon_view_get_model (icon_view);
	if (! model)
		return;

	gtk_tree_model_get_iter (model,
				 &iter,
				 path);
	gtk_tree_model_get (model, &iter,
			    1, &filename,
			    -1);
	
	if (filename) {
		set_face_from_filename (filename);
	}

	g_free (filename);
}

static void
selection_changed (GtkIconView *icon_list,
		   gpointer	data)
{
	gtk_icon_view_selected_foreach (icon_list,
					selection_foreach,
					data);
}

static void
setup_icon_view (GtkIconView *iconview)
{
	GtkTreeModel	*model;
	GtkCellRenderer *cell;

	gtk_icon_view_set_selection_mode (iconview,
					  GTK_SELECTION_SINGLE);

	model = create_model ();
	gtk_icon_view_set_model (iconview, model);
	fill_model (model);

	cell = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (iconview), cell, FALSE);

	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (iconview),
					cell, "pixbuf", 0, NULL);

	g_signal_connect (GTK_WIDGET (iconview), "selection_changed",
			  G_CALLBACK (selection_changed), NULL);

}

int
main (int argc, char *argv[])
{
	GtkWidget  *dialog;
	GtkWidget  *browse_button;
	GtkWidget  *face_image;
	GtkWidget  *iconview;
	gboolean    face_browser;
	gchar	   *config_file, *config_prefix;
	char	   *greeter;
	int	    max_size;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gdmphotosetup", VERSION, 
			    LIBGNOMEUI_MODULE /* module_info */,
			    argc, argv,
			    NULL);

	photofile = g_build_filename (g_get_home_dir (), ".face", NULL);

	maybe_migrate_old_config ();

        config_file = gdm_common_get_config_file ();
        if (config_file == NULL) {
                g_print (_("Could not access GDM configuration file.\n"));
                exit (EXIT_FAILURE);
        }

        config_prefix = g_strdup_printf("=%s=/", config_file);
	gnome_config_push_prefix (config_prefix);
	face_browser = gnome_config_get_bool (GDM_KEY_BROWSER);
	max_size = gnome_config_get_int (GDM_KEY_MAXFILE);
	max_width = gnome_config_get_int (GDM_KEY_ICONWIDTH);
	max_height = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
	greeter = gnome_config_get_string (GDM_KEY_GREETER);

	facedir = gnome_config_get_string (GDM_KEY_FACEDIR);
	gnome_config_pop_prefix ();
	g_free (config_prefix);
	g_free (config_file);

	gtk_window_set_default_icon_name ("stock_person");

	xml = glade_xml_new (GDM_GLADE_DIR "/gdmphotosetup.glade", NULL, NULL);

	dialog	      = glade_xml_get_widget (xml, "face_dialog");
	face_image    = glade_xml_get_widget (xml, "face_image");
	browse_button = glade_xml_get_widget (xml, "browse_button");
	iconview      = glade_xml_get_widget (xml, "face_iconview");

	if (access (photofile, R_OK) == 0) {
		gtk_image_set_from_file (GTK_IMAGE (face_image),
					 photofile);
	} else {
		gtk_image_set_from_icon_name (GTK_IMAGE (face_image),
					      "stock_person",
					      GTK_ICON_SIZE_DIALOG);
	}

	setup_icon_view (GTK_ICON_VIEW (iconview));

	g_signal_connect (browse_button, "clicked",
			  G_CALLBACK (browse_button_cb), dialog);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (dialog_response), NULL);

	gtk_widget_set_size_request (dialog, 500, 400);

	gtk_widget_show_all (dialog);
	gtk_main ();

	return 0;
}
