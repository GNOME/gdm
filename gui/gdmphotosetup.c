/*
 *    GDMphotosetup - graphical .gnome/photo setup program for users
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
#include <gnome.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <viciousui.h>

#include "gdm.h"

/* If path starts with a "trusted" directory, don't sanity check things */
static gboolean
is_in_trusted_pic_dir (const char *path)
{
	char *globalpix;

	/* our own pixmap dir is trusted */
	if (strncmp (path, EXPANDED_PIXMAPDIR, sizeof (EXPANDED_PIXMAPDIR)) == 0)
		return TRUE;

	/* gnome's pixmap dir is trusted */
	globalpix = gnome_unconditional_pixmap_file ("");
	if (strncmp (path, globalpix, strlen (globalpix)) == 0) {
		g_free (globalpix);
		return TRUE;
	}
	g_free (globalpix);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	GtkWidget *dialog;
	GtkWidget *photo;
	gboolean face_browser;
	int max_size;
	char *last_pix;

	bindtextdomain (PACKAGE, GNOMELOCALEDIR);
	textdomain (PACKAGE);

	gnome_init ("gdmphotosetup", VERSION, argc, argv);

	last_pix = gnome_config_get_string ("/gdmphotosetup/last/picture");

	gnome_config_push_prefix ("=" GDM_CONFIG_FILE "=/");
	face_browser = gnome_config_get_bool (GDM_KEY_BROWSER);
	max_size = gnome_config_get_int (GDM_KEY_MAXFILE);
	gnome_config_pop_prefix ();

	if ( ! face_browser) {
		GtkWidget *d;
		d = gnome_warning_dialog (_("The face browser is not "
					    "configured,\nplease ask your "
					    "system administrator to enable "
					    "it\nin the GDM configurator "
					    "program."));
		gnome_dialog_run_and_close (GNOME_DIALOG (d));
	}

	dialog = gnome_dialog_new (_("Select a photo"),
				   GNOME_STOCK_BUTTON_OK,
				   GNOME_STOCK_BUTTON_CANCEL,
				   NULL);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
			    gtk_label_new (_("Select a photograph to show "
					     "in the facebrowser:")),
			    FALSE, FALSE, 0);

	photo = gnome_pixmap_entry_new ("gdm_face",
					_("Browse"),
					TRUE);
	gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (dialog)->vbox),
			    photo, TRUE, TRUE, 0);

	if ( ! ve_string_empty (last_pix)) {
		GtkWidget *e = gnome_pixmap_entry_gtk_entry
			(GNOME_PIXMAP_ENTRY (photo));
		gtk_entry_set_text (GTK_ENTRY (e), last_pix);
	}

	gtk_widget_show_all (dialog);

	while (gnome_dialog_run (GNOME_DIALOG (dialog)) == 0) {
		struct stat s;
		char *pixmap = gnome_pixmap_entry_get_filename (GNOME_PIXMAP_ENTRY (photo));
		if (ve_string_empty (pixmap) ||
		    stat (pixmap, &s) < 0) {
			GtkWidget *d;
			d = gnome_warning_dialog (_("No picture selected."));
			gnome_dialog_run_and_close (GNOME_DIALOG (d));
		} else if (is_in_trusted_pic_dir (pixmap)) {
			/* Picture is in trusted dir, no need to copy nor
			 * check it */

			/* yay, leak, who cares */
			char *cfg_file = g_strconcat (g_get_home_dir (), 
						      "/.gnome/gdm",
						      NULL);
			gnome_config_set_string ("/gdm/face/picture",
						 pixmap);
			gnome_config_sync ();
			/* ensure proper permissions */
			chmod (cfg_file, 0600);
			break;
		} else if (s.st_size > max_size) {
			char *msg;
			GtkWidget *d;
			msg = g_strdup_printf (_("The picture is too large and "
						 "the system administrator\n"
						 "disallowed pictures larger "
						 "then %d bytes to\n"
						 "show in the face browser"),
					       max_size);
			d = gnome_warning_dialog (msg);
			gnome_dialog_run_and_close (GNOME_DIALOG (d));
		} else {
			char buf[4096];
			size_t size;
			/* yay, leak, who cares */
			char *photofile = g_strconcat (g_get_home_dir (), 
						       "/.gnome/photo",
						       NULL);
			char *cfg_file = g_strconcat (g_get_home_dir (), 
						      "/.gnome/gdm",
						      NULL);
			int fddest, fdsrc;

			fdsrc = open (pixmap, O_RDONLY);
			if (fdsrc < 0) {
				GtkWidget *d;
				char *msg = g_strdup_printf
					(_("File %s cannot be open for "
					   "reading\nError: %s"),
					 pixmap,
					 g_strerror (errno));
				d = gnome_warning_dialog (msg);
				gnome_dialog_run_and_close (GNOME_DIALOG (d));
				g_free (cfg_file);
				g_free (photofile);
				continue;
			}
			unlink (photofile);
			fddest = open (photofile, O_WRONLY | O_CREAT);
			if (fddest < 0) {
				GtkWidget *d;
				char *msg = g_strdup_printf
					(_("File %s cannot be open for "
					   "writing\nError: %s"),
					 photofile,
					 g_strerror (errno));
				d = gnome_warning_dialog (msg);
				gnome_dialog_run_and_close (GNOME_DIALOG (d));
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
			break;
		}
	}

	return 0;
}
