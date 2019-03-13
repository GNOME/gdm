/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 William Jon McCann <jmccann@redhat.com>
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
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <locale.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <canberra-gtk.h>

#include <X11/Xatom.h>
#include <gdk/gdkx.h>

#define SELECTION_NAME "_GDM_SCREENSHOT"
static GtkWidget *selection_window;

static gboolean debug_in;

/* Keep all config options for compatibility even if they are noops */
GOptionEntry options [] = {
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug_in, N_("Debugging output"), NULL },
        { NULL }
};

/* To make sure there is only one screenshot taken at a time,
 * (Imagine key repeat for the print screen key) we hold a selection
 * until we are done taking the screenshot
 */
/*  * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu> */
static gboolean
screenshot_grab_lock (void)
{
        Atom       selection_atom;
        GdkCursor *cursor;
        gboolean   result = FALSE;

        selection_atom = gdk_x11_get_xatom_by_name (SELECTION_NAME);
        XGrabServer (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
        if (XGetSelectionOwner (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()), selection_atom) != None) {
                goto out;
        }

        selection_window = gtk_invisible_new ();
        gtk_widget_show (selection_window);

        if (!gtk_selection_owner_set (selection_window,
                                      gdk_atom_intern (SELECTION_NAME, FALSE),
                                      GDK_CURRENT_TIME)) {
                gtk_widget_destroy (selection_window);
                selection_window = NULL;
                goto out;
        }

        cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_WATCH);
        gdk_pointer_grab (gtk_widget_get_window (selection_window), FALSE, 0, NULL,
                          cursor, GDK_CURRENT_TIME);
        g_object_unref (cursor);

        result = TRUE;

 out:
        XUngrabServer (GDK_DISPLAY_XDISPLAY (gdk_display_get_default ()));
        gdk_display_flush (gdk_display_get_default ());

        return result;
}

/*  * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu> */
static void
screenshot_release_lock (void)
{
        if (selection_window != NULL) {
                gtk_widget_destroy (selection_window);
                selection_window = NULL;
        }
        gdk_display_flush (gdk_display_get_default ());
}

/*  * Copyright (C) 2001-2006  Jonathan Blandford <jrb@alum.mit.edu> */
static GdkPixbuf *
screenshot_get_pixbuf (Window w)
{
        GdkWindow *window;
        GdkWindow *root;
        GdkPixbuf *screenshot;
        int        x_real_orig;
        int        y_real_orig;
        int        x_orig;
        int        y_orig;
        int        real_width;
        int        real_height;
        int        width;
        int        height;

        window = gdk_x11_window_foreign_new_for_display (gdk_display_get_default (), w);
        if (window == NULL) {
                return NULL;
        }

        root = gdk_x11_window_foreign_new_for_display (gdk_display_get_default (), GDK_ROOT_WINDOW ());
        gdk_window_get_geometry (window, NULL, NULL, &real_width, &real_height);
        gdk_window_get_origin (window, &x_real_orig, &y_real_orig);

        x_orig = x_real_orig;
        y_orig = y_real_orig;
        width = real_width;
        height = real_height;

        if (x_orig < 0) {
                width = width + x_orig;
                x_orig = 0;
        }
        if (y_orig < 0) {
                height = height + y_orig;
                y_orig = 0;
        }

        if (x_orig + width > gdk_screen_width ()) {
                width = gdk_screen_width () - x_orig;
        }
        if (y_orig + height > gdk_screen_height ()) {
                height = gdk_screen_height () - y_orig;
        }

        screenshot = gdk_pixbuf_get_from_window (root,
                                                 x_orig,
                                                 y_orig,
                                                 width,
                                                 height);

        return screenshot;
}

static char *
screenshot_save (GdkPixbuf *pixbuf)
{
        char       *filename;
        gboolean    res;
        GError     *error;

        filename = g_build_filename (GDM_SCREENSHOT_DIR,
                                     "GDM-Screenshot.png",
                                     NULL);

        error = NULL;
        res = gdk_pixbuf_save (pixbuf,
                               filename,
                               "png",
                               &error,
                               "tEXt::CREATOR", "gdm-screenshot",
                               NULL);
        if (! res) {
                g_warning ("Unable to save screenshot: %s", error->message);
                g_error_free (error);
                g_free (filename);
                filename = NULL;
        }

        return filename;
}

static void
sound_effect_finished (ca_context *c,
                       uint32_t    id,
                       int         error_code,
                       void       *userdata)
{
}

static void
play_sound_effect (Window xid)
{
        ca_context  *c;
        ca_proplist *p;
        int          res;

        c = ca_gtk_context_get ();

        p = NULL;
        res = ca_proplist_create (&p);
        if (res < 0) {
                goto done;
        }

        res = ca_proplist_sets (p, CA_PROP_EVENT_ID, "screen-capture");
        if (res < 0) {
                goto done;
        }

        res = ca_proplist_sets (p, CA_PROP_EVENT_DESCRIPTION, _("Screenshot taken"));
        if (res < 0) {
                goto done;
        }

        res = ca_proplist_setf (p,
                                CA_PROP_WINDOW_X11_XID,
                                "%lu",
                                (unsigned long) xid);
        if (res < 0) {
                goto done;
        }

        ca_context_play_full (c, 0, p, sound_effect_finished, NULL);

 done:
        if (p != NULL) {
                ca_proplist_destroy (p);
        }

}

static void
prepare_screenshot (void)
{
        Window     win;
        GdkPixbuf *screenshot;
        char      *filename;

        if (!screenshot_grab_lock ()) {
                exit (EXIT_SUCCESS);
        }

        win = GDK_ROOT_WINDOW ();

        screenshot = screenshot_get_pixbuf (win);

        screenshot_release_lock ();

        if (screenshot == NULL) {
                /* FIXME: dialog? */
                exit (EXIT_FAILURE);
        }

        play_sound_effect (win);

        filename = screenshot_save (screenshot);
        if (filename != NULL) {
                g_print ("Wrote %s\n", filename);
                /* FIXME: show a dialog or something */
                g_free (filename);
        }
}

int
main (int argc, char *argv[])
{
        GOptionContext *ctx;
        gboolean        res;
        GError         *error;

        bindtextdomain (GETTEXT_PACKAGE, GNOMELOCALEDIR);
        bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
        textdomain (GETTEXT_PACKAGE);
        setlocale (LC_ALL, "");

        /* Option parsing */
        ctx = g_option_context_new (N_("Take a picture of the screen"));
        g_option_context_set_translation_domain (ctx, GETTEXT_PACKAGE);
        g_option_context_add_main_entries (ctx, options, NULL);
        g_option_context_add_group (ctx, gtk_get_option_group (TRUE));
        error = NULL;
        res = g_option_context_parse (ctx, &argc, &argv, &error);
        g_option_context_free (ctx);

        if (! res) {
                g_warning ("%s", error->message);
                g_error_free (error);
                exit (EXIT_FAILURE);
        }

        prepare_screenshot ();

        return 1;
}
