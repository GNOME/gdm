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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glade/glade-xml.h>

#include "gdm-greeter.h"
#include "gdm-simple-greeter.h"
#include "gdm-common.h"
#include "gdm-settings-client.h"
#include "gdm-settings-keys.h"

#include "gdm-greeter-panel.h"
#include "gdm-greeter-background.h"
#include "gdm-user-chooser-widget.h"

#if HAVE_PAM
#include <security/pam_appl.h>
#define PW_ENTRY_SIZE PAM_MAX_RESP_SIZE
#else
#define PW_ENTRY_SIZE GDM_MAX_PASS
#endif

#define GLADE_XML_FILE "gdm-simple-greeter.glade"

#define GPM_DBUS_NAME      "org.freedesktop.PowerManagement"
#define GPM_DBUS_PATH      "/org/freedesktop/PowerManagement"
#define GPM_DBUS_INTERFACE "org.freedesktop.PowerManagement"

#define GDM_SIMPLE_GREETER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SIMPLE_GREETER, GdmSimpleGreeterPrivate))

enum {
        PAGE_USERLIST = 0,
        PAGE_AUTH
};

enum {
        RESPONSE_RESTART,
        RESPONSE_REBOOT,
        RESPONSE_CLOSE
};

struct GdmSimpleGreeterPrivate
{
        GladeXML        *xml;
        GtkWidget       *panel;
        GtkWidget       *background;
        GtkWidget       *user_chooser;
};

enum {
        PROP_0,
};

static void     gdm_simple_greeter_class_init   (GdmSimpleGreeterClass *klass);
static void     gdm_simple_greeter_init         (GdmSimpleGreeter      *simple_greeter);
static void     gdm_simple_greeter_finalize     (GObject               *object);

G_DEFINE_TYPE (GdmSimpleGreeter, gdm_simple_greeter, GDM_TYPE_GREETER)

static gboolean
gdm_simple_greeter_start (GdmGreeter *greeter)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->start (greeter);

        return TRUE;
}

static gboolean
gdm_simple_greeter_stop (GdmGreeter *greeter)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->stop (greeter);

        return TRUE;
}

static void
set_busy (GdmSimpleGreeter *greeter)
{
        GdkCursor *cursor;
        GtkWidget *top_level;

        top_level = glade_xml_get_widget (greeter->priv->xml, "auth-window");

        cursor = gdk_cursor_new (GDK_WATCH);
        gdk_window_set_cursor (top_level->window, cursor);
        gdk_cursor_unref (cursor);
}

static void
set_ready (GdmSimpleGreeter *greeter)
{
        GdkCursor *cursor;
        GtkWidget *top_level;

        top_level = glade_xml_get_widget (greeter->priv->xml, "auth-window");

        cursor = gdk_cursor_new (GDK_LEFT_PTR);
        gdk_window_set_cursor (top_level->window, cursor);
        gdk_cursor_unref (cursor);
}

static void
set_sensitive (GdmSimpleGreeter *greeter,
               gboolean          sensitive)
{
        GtkWidget *box;

        box = glade_xml_get_widget (greeter->priv->xml, "auth-input-box");
        gtk_widget_set_sensitive (box, sensitive);

        box = glade_xml_get_widget (greeter->priv->xml, "buttonbox");
        gtk_widget_set_sensitive (box, sensitive);
}

static void
set_focus (GdmSimpleGreeter *greeter)
{
        GtkWidget *top_level;
        GtkWidget *entry;

        entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-entry");
        top_level = glade_xml_get_widget (greeter->priv->xml, "auth-window");

        gdk_window_focus (top_level->window, GDK_CURRENT_TIME);

        if (! GTK_WIDGET_HAS_FOCUS (entry)) {
                gtk_widget_grab_focus (entry);
        }
}


static void
set_message (GdmSimpleGreeter *greeter,
             const char       *text)
{
        GtkWidget *label;

        label = glade_xml_get_widget (greeter->priv->xml, "auth-message-label");
        gtk_label_set_text (GTK_LABEL (label), text);
}

static void
switch_page (GdmSimpleGreeter *greeter,
             int               number)
{
        GtkWidget *notebook;

        /* switch page */
        notebook = glade_xml_get_widget (greeter->priv->xml, "notebook");
        gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), number);

}

static void
do_cancel (GdmSimpleGreeter *greeter)
{
        switch_page (greeter, PAGE_USERLIST);
        set_busy (greeter);
        set_sensitive (greeter, FALSE);

        gdm_greeter_emit_cancelled (GDM_GREETER (greeter));

        set_ready (greeter);
}

static void
reset_dialog (GdmSimpleGreeter *greeter)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_debug ("Resetting dialog");

        entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);

        label = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), "");

        set_message (greeter, "");

        switch_page (greeter, PAGE_USERLIST);

        set_sensitive (greeter, TRUE);
        set_ready (greeter);
        set_focus (GDM_SIMPLE_GREETER (greeter));
}

static gboolean
gdm_simple_greeter_ready (GdmGreeter *greeter)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        /*GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->reset (greeter);*/

        set_sensitive (GDM_SIMPLE_GREETER (greeter), TRUE);
        set_ready (GDM_SIMPLE_GREETER (greeter));
        set_focus (GDM_SIMPLE_GREETER (greeter));

        return TRUE;
}

static gboolean
gdm_simple_greeter_reset (GdmGreeter *greeter)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        /*GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->reset (greeter);*/

        reset_dialog (GDM_SIMPLE_GREETER (greeter));

        return TRUE;
}

static gboolean
gdm_simple_greeter_info (GdmGreeter *greeter,
                         const char *text)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->info (greeter, text);

        g_debug ("SIMPLE GREETER: info: %s", text);

        set_message (GDM_SIMPLE_GREETER (greeter), text);

        return TRUE;
}

static gboolean
gdm_simple_greeter_problem (GdmGreeter *greeter,
                            const char *text)
{
        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->problem (greeter, text);

        g_debug ("SIMPLE GREETER: problem: %s", text);

        set_message (GDM_SIMPLE_GREETER (greeter), text);

        return TRUE;
}

static gboolean
gdm_simple_greeter_info_query (GdmGreeter *greeter,
                               const char *text)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->info_query (greeter, text);

        g_debug ("SIMPLE GREETER: info query: %s", text);

        entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), TRUE);

        label = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        set_sensitive (GDM_SIMPLE_GREETER (greeter), TRUE);
        set_ready (GDM_SIMPLE_GREETER (greeter));
        set_focus (GDM_SIMPLE_GREETER (greeter));

        if (! GTK_WIDGET_HAS_FOCUS (entry)) {
                gtk_widget_grab_focus (entry);
        }

        return TRUE;
}

static gboolean
gdm_simple_greeter_secret_info_query (GdmGreeter *greeter,
                                      const char *text)
{
        GtkWidget  *entry;
        GtkWidget  *label;

        g_return_val_if_fail (GDM_IS_GREETER (greeter), FALSE);

        GDM_GREETER_CLASS (gdm_simple_greeter_parent_class)->secret_info_query (greeter, text);

        g_debug ("SIMPLE GREETER: secret info query: %s", text);

        entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-entry");
        gtk_entry_set_text (GTK_ENTRY (entry), "");
        gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);

        label = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-label");
        gtk_label_set_text (GTK_LABEL (label), text);

        set_sensitive (GDM_SIMPLE_GREETER (greeter), TRUE);
        set_ready (GDM_SIMPLE_GREETER (greeter));
        set_focus (GDM_SIMPLE_GREETER (greeter));

        return TRUE;
}

static void
gdm_simple_greeter_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
        GdmSimpleGreeter *self;

        self = GDM_SIMPLE_GREETER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_simple_greeter_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
        GdmSimpleGreeter *self;

        self = GDM_SIMPLE_GREETER (object);

        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
log_in_button_clicked (GtkButton        *button,
                       GdmSimpleGreeter *greeter)
{
        gboolean    res;
        GtkWidget  *entry;
        const char *text;

        set_busy (greeter);
        set_sensitive (greeter, FALSE);

        entry = glade_xml_get_widget (greeter->priv->xml, "auth-prompt-entry");
        text = gtk_entry_get_text (GTK_ENTRY (entry));
        res = gdm_greeter_emit_answer_query (GDM_GREETER (greeter), text);
}

static void
cancel_button_clicked (GtkButton        *button,
                       GdmSimpleGreeter *greeter)
{
        do_cancel (greeter);
}

static void
on_user_activated (GdmUserChooserWidget *user_chooser,
                   GdmSimpleGreeter     *greeter)
{
        char *user_name;

        user_name = gdm_user_chooser_widget_get_current_user_name (GDM_USER_CHOOSER_WIDGET (greeter->priv->user_chooser));

        gdm_greeter_emit_begin_verification (GDM_GREETER (greeter),
                                             user_name);
        switch_page (greeter, PAGE_AUTH);
}

#define INVISIBLE_CHAR_DEFAULT       '*'
#define INVISIBLE_CHAR_BLACK_CIRCLE  0x25cf
#define INVISIBLE_CHAR_WHITE_BULLET  0x25e6
#define INVISIBLE_CHAR_BULLET        0x2022
#define INVISIBLE_CHAR_NONE          0

static gboolean
launch_compiz (GdmSimpleGreeter *greeter)
{
        GError  *error;
        gboolean ret;

        g_debug ("Launching compiz");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("gtk-window-decorator --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        error = NULL;
        g_spawn_command_line_async ("compiz --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

        /* FIXME: should try to detect if it actually works */

 out:
        return ret;
}

static gboolean
launch_metacity (GdmSimpleGreeter *greeter)
{
        GError  *error;
        gboolean ret;

        g_debug ("Launching metacity");

        ret = FALSE;

        error = NULL;
        g_spawn_command_line_async ("metacity --replace", &error);
        if (error != NULL) {
                g_warning ("Error starting WM: %s", error->message);
                g_error_free (error);
                goto out;
        }

        ret = TRUE;

 out:
        return ret;
}

static void
create_greeter (GdmSimpleGreeter *greeter)
{
        GtkWidget *dialog;
        GtkWidget *entry;
        GtkWidget *button;
        GtkWidget *box;

        if (! launch_compiz (greeter)) {
                launch_metacity (greeter);
        }

        greeter->priv->user_chooser = gdm_user_chooser_widget_new ();
        g_signal_connect (greeter->priv->user_chooser,
                          "user-activated",
                          G_CALLBACK (on_user_activated),
                          greeter);

        gtk_widget_show_all (greeter->priv->user_chooser);

        greeter->priv->xml = glade_xml_new (GLADEDIR "/" GLADE_XML_FILE, NULL, PACKAGE);
        if (greeter->priv->xml == NULL) {
                /* FIXME: */
        }

        dialog = glade_xml_get_widget (greeter->priv->xml, "auth-window");
        if (dialog == NULL) {
                /* FIXME: */
        }

        box = glade_xml_get_widget (greeter->priv->xml, "userlist-box");
        if (box == NULL) {
                g_warning ("Userlist box not found");
                /* FIXME: */
        }
        gtk_container_add (GTK_CONTAINER (box), greeter->priv->user_chooser);

        button = glade_xml_get_widget (greeter->priv->xml, "log-in-button");
        if (dialog != NULL) {
                gtk_widget_grab_default (button);
                g_signal_connect (button, "clicked", G_CALLBACK (log_in_button_clicked), greeter);
        }

        button = glade_xml_get_widget (greeter->priv->xml, "cancel-button");
        if (dialog != NULL) {
                g_signal_connect (button, "clicked", G_CALLBACK (cancel_button_clicked), greeter);
        }

        entry = glade_xml_get_widget (GDM_SIMPLE_GREETER (greeter)->priv->xml, "auth-prompt-entry");
        /* Only change the invisible character if it '*' otherwise assume it is OK */
        if ('*' == gtk_entry_get_invisible_char (GTK_ENTRY (entry))) {
                gunichar invisible_char;
                invisible_char = INVISIBLE_CHAR_BLACK_CIRCLE;
                gtk_entry_set_invisible_char (GTK_ENTRY (entry), invisible_char);
        }

        gtk_window_set_opacity (GTK_WINDOW (dialog), 0.75);

        gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_set_deletable (GTK_WINDOW (dialog), FALSE);
        gtk_window_set_decorated (GTK_WINDOW (dialog), FALSE);
        gtk_window_set_skip_taskbar_hint (GTK_WINDOW (dialog), TRUE);
        gtk_window_set_skip_pager_hint (GTK_WINDOW (dialog), TRUE);
        gtk_window_stick (GTK_WINDOW (dialog));
        gtk_widget_show (dialog);
}

static void
create_panel (GdmSimpleGreeter *greeter)
{
        greeter->priv->background = gdm_greeter_background_new ();
        gtk_widget_show (greeter->priv->background);
        greeter->priv->panel = gdm_greeter_panel_new ();
        gtk_widget_show (greeter->priv->panel);
}

static GObject *
gdm_simple_greeter_constructor (GType                  type,
                                guint                  n_construct_properties,
                                GObjectConstructParam *construct_properties)
{
        GdmSimpleGreeter      *greeter;
        GdmSimpleGreeterClass *klass;

        klass = GDM_SIMPLE_GREETER_CLASS (g_type_class_peek (GDM_TYPE_SIMPLE_GREETER));

        greeter = GDM_SIMPLE_GREETER (G_OBJECT_CLASS (gdm_simple_greeter_parent_class)->constructor (type,
                                                                                                     n_construct_properties,
                                                                                                     construct_properties));
        create_greeter (greeter);
        create_panel (greeter);

        return G_OBJECT (greeter);
}

static void
gdm_simple_greeter_class_init (GdmSimpleGreeterClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);
        GdmGreeterClass *greeter_class = GDM_GREETER_CLASS (klass);

        object_class->get_property = gdm_simple_greeter_get_property;
        object_class->set_property = gdm_simple_greeter_set_property;
        object_class->constructor = gdm_simple_greeter_constructor;
        object_class->finalize = gdm_simple_greeter_finalize;

        greeter_class->start = gdm_simple_greeter_start;
        greeter_class->stop = gdm_simple_greeter_stop;
        greeter_class->ready = gdm_simple_greeter_ready;
        greeter_class->reset = gdm_simple_greeter_reset;
        greeter_class->info = gdm_simple_greeter_info;
        greeter_class->problem = gdm_simple_greeter_problem;
        greeter_class->info_query = gdm_simple_greeter_info_query;
        greeter_class->secret_info_query = gdm_simple_greeter_secret_info_query;

        g_type_class_add_private (klass, sizeof (GdmSimpleGreeterPrivate));
}

static void
gdm_simple_greeter_init (GdmSimpleGreeter *simple_greeter)
{

        sleep (15);
        simple_greeter->priv = GDM_SIMPLE_GREETER_GET_PRIVATE (simple_greeter);

}

static void
gdm_simple_greeter_finalize (GObject *object)
{
        GdmSimpleGreeter *simple_greeter;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SIMPLE_GREETER (object));

        simple_greeter = GDM_SIMPLE_GREETER (object);

        g_return_if_fail (simple_greeter->priv != NULL);

        G_OBJECT_CLASS (gdm_simple_greeter_parent_class)->finalize (object);
}

GdmGreeter *
gdm_simple_greeter_new (const char *display_id)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_SIMPLE_GREETER,
                               "display-id", display_id,
                               NULL);

        return GDM_GREETER (object);
}
