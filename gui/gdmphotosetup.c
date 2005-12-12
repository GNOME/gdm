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
#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include "gdm.h"
#include "gdmcommon.h"
#include "gdmconfig.h"
#include "ve-miscui.h"

static GladeXML *xml;
static char	*photofile;
static char	*facedir;
static char	*imagename;
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

static GdkPixbuf *
create_preview_pixbuf (const gchar *uri) 
{
	GdkPixbuf *pixbuf = NULL;
	
	if ((uri != NULL) && (uri[0] != '\0')) {
    
		gchar *file = NULL;
		
		if (g_path_is_absolute (uri) == TRUE) {
			file = g_strdup (uri);
		}
		else {
			/* URIs are local, because gtk_file_chooser_get_local_only() is true. */
			file = g_filename_from_uri (uri, NULL, NULL);	
		}
		
		if (file != NULL) {

			GdkPixbufFormat *info;
			gint width;
			gint height;

			info = gdk_pixbuf_get_file_info (file, &width, &height);
			
			if (width > 128 || height > 128) {
				pixbuf = gdk_pixbuf_new_from_file_at_size (file, 128, 128, NULL);
			}
			else {
				pixbuf = gdk_pixbuf_new_from_file (file, NULL);
			}
			g_free (file);
		}
	}				
	return pixbuf;
}

static void 
update_preview_cb (GtkFileChooser *chooser) 
{
	GtkWidget *image;
	gchar *uri;

	image = gtk_file_chooser_get_preview_widget (GTK_FILE_CHOOSER (chooser));
	uri = gtk_file_chooser_get_preview_uri (chooser);
  
	if (uri != NULL) {
  
		GdkPixbuf *pixbuf = NULL;
    
		pixbuf = create_preview_pixbuf (uri);

		if (pixbuf != NULL) {
			gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
			g_object_unref (pixbuf);
		}
		else {
			gtk_image_set_from_stock (GTK_IMAGE (image),
			                          "gtk-dialog-question",
			                          GTK_ICON_SIZE_DIALOG);
		}
	}		
	gtk_file_chooser_set_preview_widget_active (chooser, TRUE);
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
		char      *msg;
		
		msg = g_strdup_printf (_("File %s cannot be opened for "
					 "writing."), tmp);

		d = ve_hig_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_ERROR,
				       GTK_BUTTONS_OK,
				       _("Cannot open file"),
				       msg);

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);

		g_free (tmp);
		g_free (msg);
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
	if (response == GTK_RESPONSE_ACCEPT) {
		g_free (imagename);
		imagename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
		set_face_from_filename (imagename);
	}

	gtk_widget_destroy (file_dialog);
}

static void
add_preview_widget (GtkWidget *widget)
{
	GtkWidget *image;

	image = gtk_image_new ();
	gtk_widget_set_size_request (image, 128, 128);

	gtk_file_chooser_set_preview_widget (GTK_FILE_CHOOSER (widget), image);

	gtk_image_set_from_pixbuf (GTK_IMAGE (image),
	                           create_preview_pixbuf (imagename));

	g_signal_connect (widget, "update-preview",
		          G_CALLBACK (update_preview_cb), NULL);
}

static void
browse_button_cb (GtkWidget *widget, gpointer data)
{
	GtkWindow     *parent = GTK_WINDOW (data);
	GtkFileFilter *filter;
	GtkWidget     *file_dialog;

	file_dialog = gtk_file_chooser_dialog_new (_("Select User Image"),
						   parent,
						   GTK_FILE_CHOOSER_ACTION_OPEN,
						   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
						   GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
						   NULL);

	gtk_file_chooser_set_show_hidden (GTK_FILE_CHOOSER (file_dialog), FALSE);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (file_dialog), TRUE);
	gtk_file_chooser_set_use_preview_label (GTK_FILE_CHOOSER (file_dialog), FALSE);

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

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("Images"));
	gtk_file_filter_add_pixbuf_formats (filter);
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("All Files"));
	gtk_file_filter_add_pattern(filter, "*");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	if (imagename != NULL) {
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_dialog), imagename);
	}

	add_preview_widget (file_dialog);

	g_signal_connect (G_OBJECT (file_dialog), "response",
			  G_CALLBACK (install_response), NULL);

	gtk_widget_show (file_dialog);
}

int
main (int argc, char *argv[])
{
	GtkWidget  *dialog;
	GtkWidget  *browse_button;
	GtkWidget  *face_image;
	gboolean    face_browser;
	char	   *greeter;
	int	    max_size;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
	gtk_init(&argc, &argv);
	photofile = g_build_filename (g_get_home_dir (), ".face", NULL);

	face_browser = gdm_config_get_bool (GDM_KEY_BROWSER);
	max_size     = gdm_config_get_int (GDM_KEY_USER_MAX_FILE);
	max_width    = gdm_config_get_int (GDM_KEY_MAX_ICON_WIDTH);
	max_height   = gdm_config_get_int (GDM_KEY_MAX_ICON_HEIGHT);
	greeter      = gdm_config_get_string (GDM_KEY_GREETER);
	facedir      = gdm_config_get_string (GDM_KEY_GLOBAL_FACE_DIR);
	imagename    = NULL;

	gtk_window_set_default_icon_name ("stock_person");

	xml = glade_xml_new (GDM_GLADE_DIR "/gdmphotosetup.glade", NULL, NULL);

	dialog	      = glade_xml_get_widget (xml, "face_dialog");
	face_image    = glade_xml_get_widget (xml, "face_image");
	browse_button = glade_xml_get_widget (xml, "browse_button");

	gtk_widget_set_size_request (browse_button, MAX (max_width, 230), MAX (max_height, 130));

	if (access (photofile, R_OK) == 0) {
		gtk_image_set_from_file (GTK_IMAGE (face_image),
					 photofile);
	} else {
		gtk_image_set_from_icon_name (GTK_IMAGE (face_image),
					      "stock_person",
					      GTK_ICON_SIZE_DIALOG);
	}

	g_signal_connect (browse_button, "clicked",
			  G_CALLBACK (browse_button_cb), dialog);

	g_signal_connect (dialog, "response",
			  G_CALLBACK (dialog_response), NULL);

	gtk_widget_show_all (dialog);
	gtk_main ();

	return 0;
}
