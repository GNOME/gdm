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
#include <libgnome/libgnome.h>
#include <libgnomeui/libgnomeui.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <viciousui.h>

#include "gdm.h"

int response = -999;

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
		fp = fopen (pidfile, "r");
	if (fp != NULL) {
		fscanf (fp, "%ld", &pid);
		fclose (fp);
	}

	g_free (pidfile);

	errno = 0;
	if (pid <= 1 ||
	    (kill (pid, 0) < 0 &&
	     errno != EPERM)) {
		dialog = gtk_message_dialog_new
			(NULL /* parent */,
			 GTK_DIALOG_MODAL /* flags */,
			 GTK_MESSAGE_WARNING,
			 GTK_BUTTONS_OK,
			 "foo");
		 gtk_label_set_markup
			 (GTK_LABEL (GTK_MESSAGE_DIALOG (dialog)->label),
			  _("<b>GDM (The GNOME Display Manager) "
			    "is not running.</b>\n\n"
			    "You might in fact be using a different "
			    "display manager, such as KDM "
			    "(KDE Display Manager or xdm).\n"
			    "If you still wish to use this feature, "
			    "either start GDM your self or ask your "
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

int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	GtkWidget *photo;
	gboolean face_browser;
	int max_size;
	char *last_pix;

	bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gnome_program_init ("gdmphotosetup", VERSION, 
			    LIBGNOMEUI_MODULE /* module_info */,
			    argc, argv,
			    NULL);

	last_pix = gnome_config_get_string ("/gdmphotosetup/last/picture");

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	face_browser = gnome_config_get_bool (GDM_KEY_BROWSER);
	max_size = gnome_config_get_int (GDM_KEY_MAXFILE);
	gnome_config_pop_prefix ();

	if ( ! gdm_check ()) {
		return 1;
	}

	if ( ! face_browser) {
		GtkWidget *d;
		d = gtk_message_dialog_new (NULL /* parent */,
					    GTK_DIALOG_MODAL /* flags */,
					    GTK_MESSAGE_WARNING,
					    GTK_BUTTONS_OK,
					    _("The face browser is not "
					      "configured,\nplease ask your "
					      "system administrator to enable "
					      "it\nin the GDM configurator "
					      "program."));
		gtk_dialog_run (GTK_DIALOG (d));
		gtk_widget_destroy (d);
	}

	dialog = gtk_dialog_new_with_buttons (_("Select a photo"),
					      NULL /* parent */,
					      0 /* flags */,
					      GTK_STOCK_CANCEL,
					      GTK_RESPONSE_CANCEL,
					      GTK_STOCK_OK,
					      GTK_RESPONSE_OK,
					      NULL);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    gtk_label_new (_("Select a photograph to show "
					     "in the facebrowser:")),
			    FALSE, FALSE, 0);

	photo = gnome_pixmap_entry_new ("gdm_face",
					_("Browse"),
					TRUE);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox),
			    photo, TRUE, TRUE, 0);

	if ( ! ve_string_empty (last_pix)) {
		gnome_file_entry_set_filename (GNOME_FILE_ENTRY (photo),
					       last_pix);
	}

	gtk_widget_show_all (dialog);

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (dialog_response),
			  NULL);

	for (;;) {
		struct stat s;
		char *pixmap;

		gtk_main ();

		if (response != GTK_RESPONSE_OK)
			break;

		pixmap = gnome_pixmap_entry_get_filename (GNOME_PIXMAP_ENTRY (photo));
		if (ve_string_empty (pixmap) ||
		    stat (pixmap, &s) < 0) {
			GtkWidget *d;
			d = gtk_message_dialog_new (NULL /* parent */,
						    GTK_DIALOG_MODAL /* flags */,
						    GTK_MESSAGE_WARNING,
						    GTK_BUTTONS_OK,
						    _("No picture selected."));
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
		} else if (s.st_size > max_size) {
			GtkWidget *d;
			d = gtk_message_dialog_new (NULL /* parent */,
						    GTK_DIALOG_MODAL /* flags */,
						    GTK_MESSAGE_WARNING,
						    GTK_BUTTONS_OK,
						    _("The picture is too large and "
						      "the system administrator\n"
						      "disallowed pictures larger "
						      "then %d bytes to\n"
						      "show in the face browser"),
						    max_size);
			gtk_dialog_run (GTK_DIALOG (d));
			gtk_widget_destroy (d);
		} else {
			char buf[4096];
			size_t size;
			char *photofile = g_strconcat (g_get_home_dir (),
						       "/.gnome2/photo",
						       NULL);
			char *cfg_file = g_strconcat (g_get_home_dir (),
						      "/.gnome2/gdm",
						      NULL);
			int fddest, fdsrc;

			fdsrc = open (pixmap, O_RDONLY);
			if (fdsrc < 0) {
				GtkWidget *d;
				d = gtk_message_dialog_new (NULL /* parent */,
							    GTK_DIALOG_MODAL /* flags */,
							    GTK_MESSAGE_ERROR,
							    GTK_BUTTONS_OK,
							    _("File %s cannot be open for "
							      "reading\nError: %s"),
							    pixmap,
							    g_strerror (errno));
				gtk_dialog_run (GTK_DIALOG (d));
				gtk_widget_destroy (d);
				g_free (cfg_file);
				g_free (photofile);
				continue;
			}
			unlink (photofile);
			fddest = open (photofile, O_WRONLY | O_CREAT);
			if (fddest < 0) {
				GtkWidget *d;
				d = gtk_message_dialog_new (NULL /* parent */,
							    GTK_DIALOG_MODAL /* flags */,
							    GTK_MESSAGE_ERROR,
							    GTK_BUTTONS_OK,
							    _("File %s cannot be open for "
							      "writing\nError: %s"),
							    photofile,
							    g_strerror (errno));
				gtk_dialog_run (GTK_DIALOG (d));
				gtk_widget_destroy (d);
				g_free (cfg_file);
				g_free (photofile);
				close (fdsrc);
				continue;
			}
			while ((size = read (fdsrc, buf, sizeof (buf))) > 0) {
				write (fddest, buf, size);
			}
			fchmod (fddest, 0600);
			close (fdsrc);
			close (fddest);
			gnome_config_set_string ("/gdmphotosetup/last/picture",
						 pixmap);
			gnome_config_set_string ("/gdm/face/picture", "");
			gnome_config_sync ();
			/* ensure proper permissions */
			chmod (cfg_file, 0600);
			g_free (cfg_file);
			g_free (photofile);
			break;
		}
	}

	gtk_widget_destroy (dialog);

	return 0;
}
