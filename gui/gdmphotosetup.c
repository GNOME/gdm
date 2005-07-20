/*
 *    GDMphotosetup - graphical .gnome2/photo setup program for users
 *    (c)2001 Queen of England
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
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>

#include <viciousui.h>

#include "gdm.h"

static GtkWidget *preview;
static GtkWidget *current_image;
static char *current_pix;
static char *photofile;
static char *facedir;
static int max_width, max_height;
static int response = -999;

static gboolean
gdm_check (void)
{
	GtkWidget *dialog;
	FILE *fp = NULL;
	long pid;
	char *pidfile;

	pidfile = ve_config_get_string (ve_config_get (GDM_CONFIG_FILE),
					GDM_KEY_PIDFILE);

	pid = 0;
	if (pidfile != NULL)
		VE_IGNORE_EINTR (fp = fopen (pidfile, "r"));
	if (fp != NULL) {
		int r;
		VE_IGNORE_EINTR (r = fscanf (fp, "%ld", &pid));
		VE_IGNORE_EINTR (fclose (fp));
		if (r != 1)
			pid = 0;
	}

	g_free (pidfile);

	errno = 0;
	if (pid <= 1 ||
	    (kill (pid, 0) < 0 &&
	     errno != EPERM)) {
		dialog = ve_hig_dialog_new
			(NULL /* parent */,
			 GTK_DIALOG_MODAL /* flags */,
			 GTK_MESSAGE_WARNING,
			 GTK_BUTTONS_OK,
			 FALSE /* markup */,
			 _("GDM (The GNOME Display Manager) "
			   "is not running."),
			 "%s\n%s",
			 _("You might in fact be using a different "
			   "display manager, such as KDM "
			   "(KDE Display Manager) or xdm."),
			 _("If you still wish to use this feature, "
			   "either start GDM yourself or ask your "
			   "system administrator to start GDM."));

		gtk_widget_show_all (dialog);
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
		return FALSE;
	}

	return TRUE;
}

static void
dialog_response (GtkWidget *dialog, int res, gpointer data)
{
	response = res;
	gtk_main_quit ();
}

static void
update_preview_cb (GtkFileChooser *file_chooser, gpointer data)
{
	char *filename = gtk_file_chooser_get_preview_filename (file_chooser);

	if (filename != NULL) {
		GdkPixbuf *pixbuf;
		gboolean have_preview;

		pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
		have_preview = (pixbuf != NULL);
		g_free (filename);

		if (pixbuf) {
			GdkPixbuf *preview_pixbuf;
			float scale_factor_x = 1.0;
			float scale_factor_y = 1.0;
			float scale_factor = 1.0;

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
				preview_pixbuf = pixbuf;
			} else {
				int scale_x = (int) (gdk_pixbuf_get_width (pixbuf) *
					scale_factor);
				int scale_y = (int) (gdk_pixbuf_get_height (pixbuf) *
					scale_factor);

				/* Scale bigger dimension to max icon height/width */
				preview_pixbuf = gdk_pixbuf_scale_simple (pixbuf,
					scale_x, scale_y, GDK_INTERP_BILINEAR);
			}

			gtk_image_set_from_pixbuf (GTK_IMAGE (preview), preview_pixbuf);

			gdk_pixbuf_unref (pixbuf);
			if (scale_factor != 1.0)
				gdk_pixbuf_unref (preview_pixbuf);

			gtk_file_chooser_set_preview_widget_active (file_chooser,
				have_preview);
		}
	}
}

static void
install_response (GtkWidget *file_dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_ACCEPT) {
		gtk_image_set_from_pixbuf (GTK_IMAGE (current_image),
			gtk_image_get_pixbuf (GTK_IMAGE (preview)));
		current_pix = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (file_dialog));
	} else {
		gtk_image_set_from_pixbuf (GTK_IMAGE (preview),
			gtk_image_get_pixbuf (GTK_IMAGE (current_image)));
	}

	gtk_widget_destroy (file_dialog);
}

static void
browse_button_cb (GtkWidget *widget, gpointer data)
{
	GtkWindow *parent = GTK_WINDOW (data);
	GtkFileFilter *filter;
	GtkWidget *file_dialog = gtk_file_chooser_dialog_new (_("Open File"),
					      parent,
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
					      NULL);

	if (current_pix != NULL && strcmp (photofile, current_pix))
		gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (file_dialog),
			current_pix);
	else if (facedir != NULL && g_file_test (facedir, G_FILE_TEST_IS_DIR))
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
			facedir);
	else
		gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (file_dialog),
			EXPANDED_DATADIR "/pixmaps");

	filter = gtk_file_filter_new ();
	gtk_file_filter_set_name (filter, _("PNG and JPEG"));
	gtk_file_filter_add_mime_type (filter, "image/jpeg");
	gtk_file_filter_add_mime_type (filter, "image/png");
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (file_dialog), filter);

	g_signal_connect (file_dialog, "update-preview",
			  G_CALLBACK (update_preview_cb), NULL);
        g_signal_connect (G_OBJECT (file_dialog), "destroy",
                          G_CALLBACK (gtk_widget_destroyed), &file_dialog);
        g_signal_connect (G_OBJECT (file_dialog), "response",
                          G_CALLBACK (install_response), NULL);

	gtk_widget_show (file_dialog);
}

int
main (int argc, char *argv[])
{
	struct stat s;
	GtkWidget *dialog;
	GtkWidget *browse_button;
	GtkWidget *scrolled_window;
	GtkWidget *table;
	gboolean face_browser;
	char *greeter;
	char *remotegreeter;
	int max_size;

	photofile = g_strconcat (g_get_home_dir (), "/.face", NULL);

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gdmphotosetup", VERSION, 
			    LIBGNOMEUI_MODULE /* module_info */,
			    argc, argv,
			    NULL);

	current_pix = gnome_config_get_string ("/gdmphotosetup/last/picture");

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	face_browser = gnome_config_get_bool (GDM_KEY_BROWSER);
	max_size = gnome_config_get_int (GDM_KEY_MAXFILE);
        max_width = gnome_config_get_int (GDM_KEY_ICONWIDTH);
	max_height = gnome_config_get_int (GDM_KEY_ICONHEIGHT);
	greeter = gnome_config_get_string (GDM_KEY_GREETER);
	remotegreeter = gnome_config_get_string (GDM_KEY_REMOTEGREETER);
	facedir = gnome_config_get_string (GDM_KEY_FACEDIR);
	gnome_config_pop_prefix ();

	if ( ! gdm_check ()) {
		return 1;
	}

	/* HACK */
	/* only warn if gdmlogin is set for both local and remote greeter,
	 * the themed greeter does a different setup thingie for
	 * the face browser and it would be hard to figure out here ... */
	if ( ! face_browser &&
	     strstr (greeter, "gdmlogin") != NULL &&
	     strstr (remotegreeter, "gdmlogin") != NULL) {
		GtkWidget *d;
		d = ve_hig_dialog_new (NULL /* parent */,
				       GTK_DIALOG_MODAL /* flags */,
				       GTK_MESSAGE_WARNING,
				       GTK_BUTTONS_OK,
				       FALSE /* markup */,
				       _("The face browser is not "
					 "configured"),
				       "%s",
				       _("The face browser is not configured in the "
					 "GDM configuration.  Please ask your "
					 "system administrator to enable "
					 "this feature."));

		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
	}
	g_free (greeter);
	g_free (remotegreeter);

	dialog = gtk_dialog_new_with_buttons (_("Login Photo"),
					      NULL /* parent */,
					      0 /* flags */,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    gtk_label_new (_("Select a photograph to show "
					     "in the facebrowser:")),
			    FALSE, FALSE, 0);

	if (g_stat (current_pix, &s) < 0 || current_pix == NULL) {
		preview       = gtk_image_new ();
		current_image = gtk_image_new ();
	} else {
		preview       = gtk_image_new_from_file (current_pix);
		current_image = gtk_image_new_from_file (current_pix);
	}


	table = gtk_table_new (1, 1, FALSE);
	scrolled_window = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_set_usize (scrolled_window, 128, 128);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
	   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_add_with_viewport (
	  GTK_SCROLLED_WINDOW (scrolled_window), preview);
	gtk_container_add (GTK_CONTAINER (table), scrolled_window);
	
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    table, TRUE, TRUE, 0);

	browse_button = gtk_button_new_with_mnemonic (_("_Browse"));

	g_signal_connect (G_OBJECT (browse_button), "clicked",
			  G_CALLBACK (browse_button_cb), dialog);

	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    browse_button, FALSE, TRUE, 0);

	gtk_widget_show_all (dialog);

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response),
			  NULL);

	for (;;) {
		gtk_main ();

		if (response != GTK_RESPONSE_OK)
			break;

		if (ve_string_empty (current_pix) ||
		    g_stat (current_pix, &s) < 0) {

			/*
			 * This can happen if the user has a setting for their face
			 * image, but the file does not exist.
			 */
			GtkWidget *d;
			d = ve_hig_dialog_new (NULL /* parent */,
					       GTK_DIALOG_MODAL /* flags */,
					       GTK_MESSAGE_WARNING,
					       GTK_BUTTONS_OK,
					       FALSE /* markup */,
					       _("No picture selected."),
					       /* avoid warning */ "%s", "");
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
		} else {
			GdkPixbuf *preview_pixbuf;
			char *cfg_file = g_strconcat (g_get_home_dir (),
						      "/.gnome2/gdm",
						      NULL);

			VE_IGNORE_EINTR (g_unlink (photofile));
			preview_pixbuf = gtk_image_get_pixbuf (GTK_IMAGE (current_image));

			if (gdk_pixbuf_save (preview_pixbuf, photofile, "png", NULL,
				NULL) != TRUE) {

				GtkWidget *d;
				char *tmp = ve_filename_to_utf8 (photofile);

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
				g_free (cfg_file);
				g_free (photofile);
				continue;
			}

			/* Set configuration */
			gnome_config_set_string ("/gdmphotosetup/last/picture",
						 photofile);
			gnome_config_set_string ("/gdm/face/picture", "");
			gnome_config_sync ();

                        /* Change to g_chmod after glib 2.8 release */
			chmod (cfg_file, 0600);
			chmod (photofile, 0644);

			g_free (cfg_file);
			g_free (photofile);
			break;
		}
	}

	g_free (current_pix);
	gtk_widget_destroy (dialog);

	return 0;
}
