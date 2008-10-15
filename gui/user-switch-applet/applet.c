/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
 * Copyright (C) 2008      Red Hat, Inc.
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

#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include <glade/glade-xml.h>

#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-ui-util.h>

#include <libgnome/gnome-init.h>

#include <panel-applet.h>
#include <panel-applet-gconf.h>

#include "gdm-user-manager.h"
#include "gdm-user-menu-item.h"

#define LOCKDOWN_DIR    "/desktop/gnome/lockdown"
#define LOCKDOWN_KEY    LOCKDOWN_DIR "/disable_user_switching"

typedef struct _GdmAppletData
{
        PanelApplet    *applet;

        GConfClient    *client;
        GdmUserManager *manager;
        GdmUser        *user;

        GtkWidget      *menubar;
        GtkWidget      *menuitem;
        GtkWidget      *menu;
        GtkWidget      *control_panel_item;
        GtkWidget      *separator_item;
        GtkWidget      *lock_screen_item;
        GtkWidget      *login_screen_item;
        GtkWidget      *quit_session_item;
        GSList         *items;

        gboolean        has_other_users;

        guint           client_notify_lockdown_id;

        guint           user_icon_changed_id;
        guint           user_notify_id;
        GQuark          user_menu_item_quark;
        gint8           pixel_size;
        gint            panel_size;
        GtkIconSize     icon_size;
} GdmAppletData;

typedef struct _SelectorResponseData
{
        GdmAppletData  *adata;
        GtkRadioButton *radio;
} SelectorResponseData;

static GtkTooltips *tooltips = NULL;

static void reset_icon (GdmAppletData *adata);

static gboolean applet_factory (PanelApplet   *applet,
                                const char    *iid,
                                gpointer       data);

PANEL_APPLET_BONOBO_FACTORY ("OAFIID:GNOME_FastUserSwitchApplet_Factory",
                             PANEL_TYPE_APPLET,
                             "gdm-user-switch-applet", "0",
                             (PanelAppletFactoryCallback)applet_factory,
                             NULL)

static void
about_me_cb (BonoboUIComponent *ui_container,
             gpointer           data,
             const char        *cname)
{
        GError *err;

        err = NULL;
        if (! g_spawn_command_line_async ("gnome-about-me", &err)) {
                g_critical ("Could not run `gnome-about-me': %s",
                            err->message);
                g_error_free (err);
                bonobo_ui_component_set_prop (ui_container,
                                              "/commands/GdmAboutMe",
                                              "hidden", "1",
                                              NULL);
        }
}

/*
 * gnome-panel/applets/wncklet/window-menu.c:window_filter_button_press()
 *
 * Copyright (C) 2005 James M. Cape.
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2001 Free Software Foundation, Inc.
 * Copyright (C) 2000 Helix Code, Inc.
 */
static gboolean
menubar_button_press_event_cb (GtkWidget      *menubar,
                               GdkEventButton *event,
                               GdmAppletData  *adata)
{
        if (event->button != 1) {
                g_signal_stop_emission_by_name (menubar, "button-press-event");
                /* Reset the login window item */
        }

        return FALSE;
}

static void
about_cb (BonoboUIComponent *ui_container,
          gpointer           data,
          const char        *cname)
{
        static const char *authors[] = {
                "James M. Cape <jcape@ignore-your.tv>",
                "Thomas Thurman <thomas@thurman.org.uk>",
                "William Jon McCann <jmccann@redhat.com>",
                NULL
        };
        static char *license[] = {
                N_("The User Switch Applet is free software; you can redistribute it and/or modify "
                   "it under the terms of the GNU General Public License as published by "
                   "the Free Software Foundation; either version 2 of the License, or "
                   "(at your option) any later version."),
                N_("This program is distributed in the hope that it will be useful, "
                   "but WITHOUT ANY WARRANTY; without even the implied warranty of "
                   "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
                   "GNU General Public License for more details."),
                N_("You should have received a copy of the GNU General Public License "
                   "along with this program; if not, write to the Free Software "
                   "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA "),
                NULL
        };
        char *license_i18n;

        license_i18n = g_strjoinv ("\n\n", license);

        gtk_show_about_dialog (NULL,
                               "version", VERSION,
                               "copyright", "Copyright \xc2\xa9 2004-2005 James M. Cape.\n"
                               "Copyright \xc2\xa9 2006 Thomas Thurman.\n"
                               "Copyright \xc2\xa9 2008 Red Hat, Inc.",
                               "comments", _("A menu to quickly switch between users."),
                               "authors", authors,
                               "license", license_i18n,
                               "wrap-license", TRUE,
                               "translator-credits", _("translator-credits"),
                               "logo-icon-name", "stock_people",
                               NULL);

        g_free (license_i18n);
}


static void
admin_cb (BonoboUIComponent *ui_container,
          gpointer           data,
          const char        *cname)
{
#ifdef USERS_ADMIN
        char   **args;
        gboolean res;
        GError  *err;

        err = NULL;
        if (!g_shell_parse_argv (USERS_ADMIN, NULL, &args, &err)) {
                g_critical ("Could not parse users and groups management command line `%s': %s",
                            USERS_ADMIN, err->message);
                return;
        }

        res = g_spawn_async (g_get_home_dir (),
                             args,
                             NULL,
                             (G_SPAWN_STDOUT_TO_DEV_NULL |
                              G_SPAWN_STDERR_TO_DEV_NULL |
                              G_SPAWN_SEARCH_PATH),
                             NULL,
                             NULL,
                             NULL,
                             &err);
        if (! res) {
                g_critical ("Could not run `%s' to manage users and groups: %s",
                            USERS_ADMIN, err->message);
                g_error_free (err);
        }
        g_strfreev (args);
#endif /* USERS_ADMIN */
}

static void
set_menuitem_icon (BonoboUIComponent *component,
                   const char        *item_path,
                   GtkIconTheme      *theme,
                   const char        *icon_name,
                   gint               icon_size)
{
        GdkPixbuf *pixbuf;
        int        width;
        int        height;

        pixbuf = gtk_icon_theme_load_icon (theme, icon_name, icon_size, 0, NULL);
        if (pixbuf == NULL) {
                return;
        }

        width = gdk_pixbuf_get_width (pixbuf);
        height = gdk_pixbuf_get_height (pixbuf);
        if (width > icon_size + 4 || height > icon_size + 4) {
                GdkPixbuf *tmp;
                if (height > width) {
                        width *= (gdouble) icon_size / (gdouble) height;
                        height = icon_size;
                } else {
                        height *= (gdouble) icon_size / (gdouble) width;
                        width = icon_size;
                }
                tmp = gdk_pixbuf_scale_simple (pixbuf, width, height, GDK_INTERP_BILINEAR);
                g_object_unref (pixbuf);
                pixbuf = tmp;
        }

        bonobo_ui_util_set_pixbuf (component, item_path, pixbuf, NULL);
        g_object_unref (pixbuf);
}

static void
applet_style_set_cb (GtkWidget *widget,
                     GtkStyle  *old_style,
                     gpointer   data)
{
        BonoboUIComponent *component;
        GdkScreen         *screen;
        GtkIconTheme      *theme;
        int                width;
        int                height;
        int                icon_size;

        if (gtk_widget_has_screen (widget)) {
                screen = gtk_widget_get_screen (widget);
        } else {
                screen = gdk_screen_get_default ();
        }

        if (gtk_icon_size_lookup_for_settings (gtk_settings_get_for_screen (screen),
                                               GTK_ICON_SIZE_MENU, &width, &height)) {
                icon_size = MAX (width, height);
        } else {
                icon_size = 16;
        }

        theme = gtk_icon_theme_get_for_screen (screen);
        component = panel_applet_get_popup_component (PANEL_APPLET (widget));

        set_menuitem_icon (component,
                           "/commands/GdmAboutMe",
                           theme,
                           "user-info",
                           icon_size);
        set_menuitem_icon (component,
                           "/commands/GdmUsersGroupsAdmin",
                           theme,
                           "stock_people",
                           icon_size);
}

static void
applet_change_background_cb (PanelApplet               *applet,
                             PanelAppletBackgroundType  type,
                             GdkColor                  *color,
                             GdkPixmap                 *pixmap,
                             GdmAppletData             *adata)
{
        GtkRcStyle *rc_style;
        GtkStyle   *style;

        gtk_widget_set_style (adata->menubar, NULL);
        rc_style = gtk_rc_style_new ();
        gtk_widget_modify_style (GTK_WIDGET (adata->menubar), rc_style);
        gtk_rc_style_unref (rc_style);

        switch (type) {
        case PANEL_NO_BACKGROUND:
                break;
        case PANEL_COLOR_BACKGROUND:
                gtk_widget_modify_bg (adata->menubar, GTK_STATE_NORMAL, color);
                break;
        case PANEL_PIXMAP_BACKGROUND:
                style = gtk_style_copy (adata->menubar->style);
                if (style->bg_pixmap[GTK_STATE_NORMAL]) {
                        g_object_unref (style->bg_pixmap[GTK_STATE_NORMAL]);
                }

                style->bg_pixmap[GTK_STATE_NORMAL] = g_object_ref (pixmap);
                gtk_widget_set_style (adata->menubar, style);
                g_object_unref (style);
                break;
        }
}

/*
 * gnome-panel/applets/wncklet/window-menu.c:window_menu_key_press_event()
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2001 Free Software Foundation, Inc.
 * Copyright (C) 2000 Helix Code, Inc.
 */
static gboolean
applet_key_press_event_cb (GtkWidget     *widget,
                           GdkEventKey   *event,
                           GdmAppletData *adata)
{
        GtkMenuShell *menu_shell;

        switch (event->keyval) {
        case GDK_KP_Enter:
        case GDK_ISO_Enter:
        case GDK_3270_Enter:
        case GDK_Return:
        case GDK_space:
        case GDK_KP_Space:
                menu_shell = GTK_MENU_SHELL (adata->menubar);
                /*
                 * We need to call _gtk_menu_shell_activate() here as is done in
                 * window_key_press_handler in gtkmenubar.c which pops up menu
                 * when F10 is pressed.
                 *
                 * As that function is private its code is replicated here.
                 */
                if (!menu_shell->active) {
                        gtk_grab_add (GTK_WIDGET (menu_shell));
                        menu_shell->have_grab = TRUE;
                        menu_shell->active = TRUE;
                }

                gtk_menu_shell_select_first (menu_shell, FALSE);
                return TRUE;
        default:
                break;
        }

        return FALSE;
}

static void
set_item_text_angle_and_alignment (GtkWidget *item,
                                   double     text_angle,
                                   float      xalign,
                                   float      yalign)
{
        GtkWidget *label;

        label = GTK_BIN (item)->child;

        gtk_label_set_angle (GTK_LABEL (label), text_angle);

        gtk_misc_set_alignment (GTK_MISC (label), xalign, yalign);
}

/*
 * gnome-panel/applets/wncklet/window-menu.c:window_menu_size_allocate()
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2001 Free Software Foundation, Inc.
 * Copyright (C) 2000 Helix Code, Inc.
 */
static void
applet_size_allocate_cb (GtkWidget     *widget,
                         GtkAllocation *allocation,
                         GdmAppletData *adata)
{
        GList            *children;
        GtkWidget        *top_item;
        PanelAppletOrient orient;
        gint              pixel_size;
        gdouble           text_angle;
        GtkPackDirection  pack_direction;
        float             text_xalign;
        float             text_yalign;

        pack_direction = GTK_PACK_DIRECTION_LTR;
        text_angle = 0.0;
        text_xalign = 0.0;
        text_yalign = 0.5;

        children = gtk_container_get_children (GTK_CONTAINER (adata->menubar));
        top_item = GTK_WIDGET (children->data);
        g_list_free (children);

        orient = panel_applet_get_orient (PANEL_APPLET (widget));

        switch (orient) {
        case PANEL_APPLET_ORIENT_UP:
        case PANEL_APPLET_ORIENT_DOWN:
                gtk_widget_set_size_request (top_item, -1, allocation->height);
                pixel_size = allocation->height - top_item->style->ythickness * 2;
                break;
        case PANEL_APPLET_ORIENT_LEFT:
                gtk_widget_set_size_request (top_item, allocation->width, -1);
                pixel_size = allocation->width - top_item->style->xthickness * 2;
                pack_direction = GTK_PACK_DIRECTION_TTB;
                text_angle = 270.0;
                text_xalign = 0.5;
                text_yalign = 0.0;
                break;
        case PANEL_APPLET_ORIENT_RIGHT:
                gtk_widget_set_size_request (top_item, allocation->width, -1);
                pixel_size = allocation->width - top_item->style->xthickness * 2;
                pack_direction = GTK_PACK_DIRECTION_BTT;
                text_angle = 90.0;
                text_xalign = 0.5;
                text_yalign = 0.0;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        gtk_menu_bar_set_pack_direction (GTK_MENU_BAR (adata->menubar),
                                         pack_direction);
        gtk_menu_bar_set_child_pack_direction (GTK_MENU_BAR (adata->menubar),
                                               pack_direction);

        set_item_text_angle_and_alignment (adata->menuitem,
                                           text_angle,
                                           text_xalign,
                                           text_yalign);

        if (adata->panel_size != pixel_size) {
                adata->panel_size = pixel_size;
                reset_icon (adata);
        }
}


static void
gdm_applet_data_free (GdmAppletData *adata)
{
        gconf_client_notify_remove (adata->client, adata->client_notify_lockdown_id);

        g_signal_handler_disconnect (adata->user, adata->user_notify_id);
        g_signal_handler_disconnect (adata->user, adata->user_icon_changed_id);

        if (adata->user != NULL) {
                g_object_unref (adata->user);
        }
        g_object_unref (adata->client);
        g_object_unref (adata->manager);
        g_object_unref (tooltips);

        g_free (adata);
}


/*
 * gnome-panel/applets/wncklet/window-menu.c:window_menu_on_expose()
 *
 * Copyright (C) 2003 Sun Microsystems, Inc.
 * Copyright (C) 2001 Free Software Foundation, Inc.
 * Copyright (C) 2000 Helix Code, Inc.
 */
static gboolean
menubar_expose_event_cb (GtkWidget      *widget,
                         GdkEventExpose *event,
                         GdmAppletData  *adata)
{
        if (GTK_WIDGET_HAS_FOCUS (adata->applet))
                gtk_paint_focus (widget->style, widget->window, GTK_WIDGET_STATE (widget),
                                 NULL, widget, "menu-applet", 0, 0, -1, -1);

        return FALSE;
}

static void
menu_style_set_cb (GtkWidget     *menu,
                   GtkStyle      *old_style,
                   GdmAppletData *adata)
{
        GtkSettings *settings;
        int          width;
        int          height;

        adata->icon_size = gtk_icon_size_from_name ("panel-menu");

        if (adata->icon_size == GTK_ICON_SIZE_INVALID) {
                adata->icon_size = gtk_icon_size_register ("panel-menu", 24, 24);
        }

        if (gtk_widget_has_screen (menu)) {
                settings = gtk_settings_get_for_screen (gtk_widget_get_screen (menu));
        } else {
                settings = gtk_settings_get_default ();
        }

        if (!gtk_icon_size_lookup_for_settings (settings, adata->icon_size,
                                                &width, &height)) {
                adata->pixel_size = -1;
        } else {
                adata->pixel_size = MAX (width, height);
        }
}

static void
menuitem_destroy_cb (GtkWidget     *menuitem,
                     GdmAppletData *adata)
{
        GSList *li;

        if (GDM_IS_USER_MENU_ITEM (menuitem)) {
                GdmUser *user;

                user = gdm_user_menu_item_get_user (GDM_USER_MENU_ITEM (menuitem));
                if (user != NULL) {
                        g_object_set_qdata (G_OBJECT (user),
                                            adata->user_menu_item_quark, NULL);
                }
        }

        g_debug ("Menuitem destroyed - removing");
        li = g_slist_find (adata->items, menuitem);
        adata->items = g_slist_delete_link (adata->items, li);
}

static void
menuitem_style_set_cb (GtkWidget     *menuitem,
                       GtkStyle      *old_style,
                       GdmAppletData *adata)
{

        if (GDM_IS_USER_MENU_ITEM (menuitem)) {
                gdm_user_menu_item_set_icon_size (GDM_USER_MENU_ITEM (menuitem),
                                                  adata->pixel_size);
        } else {
                GtkWidget *image;
                const char *icon_name;

                if (menuitem == adata->login_screen_item) {
                        icon_name = "gdm";
                } else if (menuitem == adata->lock_screen_item) {
                        icon_name = "system-lock-screen";
                } else if (menuitem == adata->quit_session_item) {
                        icon_name = "system-log-out";
                } else {
                        icon_name = GTK_STOCK_MISSING_IMAGE;
                }

                image = gtk_image_menu_item_get_image (GTK_IMAGE_MENU_ITEM (menuitem));
                gtk_image_set_pixel_size (GTK_IMAGE (image), adata->pixel_size);
                gtk_image_set_from_icon_name (GTK_IMAGE (image), icon_name,
                                              adata->icon_size);
        }
}


static void
user_notify_display_name_cb (GObject       *object,
                             GParamSpec    *pspec,
                             GdmAppletData *adata)
{
        GtkWidget *label;

        label = gtk_bin_get_child (GTK_BIN (adata->menuitem));
        gtk_label_set_text (GTK_LABEL (label), gdm_user_get_real_name (GDM_USER (object)));
}

/* Called every time the menu is displayed (and also for some reason
 * immediately it's created, which does no harm). All we have to do
 * here is kick off a request to GDM to let us know which users are
 * logged in, so we can display check marks next to their names.
 */
static gboolean
menu_expose_cb (GtkWidget *menu,
                gpointer   data)
{

        return FALSE;
}

static void
maybe_lock_screen (GdmAppletData *adata)
{
        char      *args[3];
        GError    *err;
        GdkScreen *screen;
        gboolean   use_gscreensaver = TRUE;
        gboolean   res;

        g_debug ("Attempting to lock screen");

        args[0] = g_find_program_in_path ("gnome-screensaver-command");
        if (args[0] == NULL) {
                args[0] = g_find_program_in_path ("xscreensaver-command");
                use_gscreensaver = FALSE;
        }

        if (args[0] == NULL) {
                return;
        }

        if (use_gscreensaver) {
                args[1] = "--lock";
        } else {
                args[1] = "-lock";
        }
        args[2] = NULL;

        if (gtk_widget_has_screen (GTK_WIDGET (adata->applet))) {
                screen = gtk_widget_get_screen (GTK_WIDGET (adata->applet));
        } else {
                screen = gdk_screen_get_default ();
        }

        err = NULL;
        res = gdk_spawn_on_screen (screen,
                                   g_get_home_dir (),
                                   args,
                                   NULL,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &err);
        if (! res) {
                g_warning (_("Can't lock screen: %s"), err->message);
                g_error_free (err);
        }

        if (use_gscreensaver) {
                args[1] = "--throttle";
        } else {
                args[1] = "-throttle";
        }

        err = NULL;
        res = gdk_spawn_on_screen (screen,
                                   g_get_home_dir (),
                                   args,
                                   NULL,
                                   (G_SPAWN_STDERR_TO_DEV_NULL
                                   | G_SPAWN_STDOUT_TO_DEV_NULL),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &err);
        if (! res) {
                g_warning (_("Can't temporarily set screensaver to blank screen: %s"),
                           err->message);
                g_error_free (err);
        }

        g_free (args[0]);
}

static void
do_switch (GdmAppletData *adata,
           GdmUser       *user)
{
        guint num_sessions;

        g_debug ("Do user switch");

        if (user == NULL) {
                gdm_user_manager_goto_login_session (adata->manager);
                goto out;
        }

        num_sessions = gdm_user_get_num_sessions (user);
        if (num_sessions > 0) {
                gdm_user_manager_activate_user_session (adata->manager, user);
        } else {
                gdm_user_manager_goto_login_session (adata->manager);
        }
 out:
        maybe_lock_screen (adata);
}

static void
update_switch_user (GdmAppletData *adata)
{
        GSList *users;

        users = gdm_user_manager_list_users (adata->manager);
        adata->has_other_users = FALSE;
        if (users != NULL) {
                adata->has_other_users = (g_slist_length (users) > 1);
        }
        g_slist_free (users);

        if (adata->has_other_users) {
                gtk_widget_show (adata->login_screen_item);
        } else {

                gtk_widget_hide (adata->login_screen_item);
        }
}

static void
on_manager_user_added (GdmUserManager *manager,
                       GdmUser        *user,
                       GdmAppletData  *adata)
{
        update_switch_user (adata);
}

static void
on_manager_user_removed (GdmUserManager *manager,
                         GdmUser        *user,
                         GdmAppletData  *adata)
{
        update_switch_user (adata);
}

static void
on_manager_users_loaded (GdmUserManager *manager,
                         GdmAppletData  *adata)
{
        update_switch_user (adata);
}

static void
on_control_panel_activate (GtkMenuItem   *item,
                           GdmAppletData *adata)
{
        char      *args[2];
        GError    *error;
        GdkScreen *screen;
        gboolean   res;

        args[0] = g_find_program_in_path ("gnome-control-center");
        if (args[0] == NULL) {
                return;
        }
        args[1] = NULL;

        if (gtk_widget_has_screen (GTK_WIDGET (adata->applet))) {
                screen = gtk_widget_get_screen (GTK_WIDGET (adata->applet));
        } else {
                screen = gdk_screen_get_default ();
        }

        error = NULL;
        res = gdk_spawn_on_screen (screen,
                                   g_get_home_dir (),
                                   args,
                                   NULL,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &error);
        if (! res) {
                g_warning (_("Can't lock screen: %s"), error->message);
                g_error_free (error);
        }

        g_free (args[0]);
}

static void
on_lock_screen_activate (GtkMenuItem   *item,
                         GdmAppletData *adata)
{
        maybe_lock_screen (adata);
}

static void
on_login_screen_activate (GtkMenuItem   *item,
                          GdmAppletData *adata)
{
        GdmUser *user;

        user = NULL;

        do_switch (adata, user);
}

static void
on_quit_session_activate (GtkMenuItem   *item,
                          GdmAppletData *adata)
{
        char      *args[3];
        GError    *error;
        GdkScreen *screen;
        gboolean   res;

        args[0] = g_find_program_in_path ("gnome-session-save");
        if (args[0] == NULL) {
                return;
        }

        args[1] = "--logout-dialog";
        args[2] = NULL;

        if (gtk_widget_has_screen (GTK_WIDGET (adata->applet))) {
                screen = gtk_widget_get_screen (GTK_WIDGET (adata->applet));
        } else {
                screen = gdk_screen_get_default ();
        }

        error = NULL;
        res = gdk_spawn_on_screen (screen,
                                   g_get_home_dir (),
                                   args,
                                   NULL,
                                   0,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &error);
        if (! res) {
                g_warning (_("Can't logout: %s"), error->message);
                g_error_free (error);
        }

        g_free (args[0]);
}

static void
create_sub_menu (GdmAppletData *adata)
{
        adata->menu = gtk_menu_new ();
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (adata->menuitem), adata->menu);
        g_signal_connect (adata->menu, "style-set",
                          G_CALLBACK (menu_style_set_cb), adata);
        g_signal_connect (adata->menu, "show",
                          G_CALLBACK (menu_expose_cb), adata);
        gtk_widget_show (adata->menu);

        g_signal_connect (adata->manager,
                          "users-loaded",
                          G_CALLBACK (on_manager_users_loaded),
                          adata);
        g_signal_connect (adata->manager,
                          "user-added",
                          G_CALLBACK (on_manager_user_added),
                          adata);
        g_signal_connect (adata->manager,
                          "user-removed",
                          G_CALLBACK (on_manager_user_added),
                          adata);


        adata->control_panel_item = gtk_image_menu_item_new_with_label (_("System Preferences..."));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->control_panel_item),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu),
                               adata->control_panel_item);
        g_signal_connect (adata->control_panel_item, "style-set",
                          G_CALLBACK (menuitem_style_set_cb), adata);
        g_signal_connect (adata->control_panel_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        g_signal_connect (adata->control_panel_item, "activate",
                          G_CALLBACK (on_control_panel_activate), adata);
        adata->items = g_slist_prepend (adata->items, adata->control_panel_item);
        gtk_widget_show (adata->control_panel_item);


        adata->separator_item = gtk_separator_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu), adata->separator_item);
        g_signal_connect (adata->separator_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        adata->items = g_slist_prepend (adata->items, adata->separator_item);
        gtk_widget_show (adata->separator_item);

        adata->lock_screen_item = gtk_image_menu_item_new_with_label (_("Lock Screen..."));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->lock_screen_item),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu),
                               adata->lock_screen_item);
        g_signal_connect (adata->lock_screen_item, "style-set",
                          G_CALLBACK (menuitem_style_set_cb), adata);
        g_signal_connect (adata->lock_screen_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        g_signal_connect (adata->lock_screen_item, "activate",
                          G_CALLBACK (on_lock_screen_activate), adata);
        adata->items = g_slist_prepend (adata->items, adata->lock_screen_item);
        gtk_widget_show (adata->lock_screen_item);

        adata->login_screen_item = gtk_image_menu_item_new_with_label (_("Switch User..."));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->login_screen_item),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu),
                               adata->login_screen_item);
        g_signal_connect (adata->login_screen_item, "style-set",
                          G_CALLBACK (menuitem_style_set_cb), adata);
        g_signal_connect (adata->login_screen_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        g_signal_connect (adata->login_screen_item, "activate",
                          G_CALLBACK (on_login_screen_activate), adata);
        adata->items = g_slist_prepend (adata->items, adata->login_screen_item);
        /* Only show switch user if there are other users */

        adata->quit_session_item = gtk_image_menu_item_new_with_label (_("Quit..."));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->quit_session_item),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu),
                               adata->quit_session_item);
        g_signal_connect (adata->quit_session_item, "style-set",
                          G_CALLBACK (menuitem_style_set_cb), adata);
        g_signal_connect (adata->quit_session_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        g_signal_connect (adata->quit_session_item, "activate",
                          G_CALLBACK (on_quit_session_activate), adata);
        adata->items = g_slist_prepend (adata->items, adata->quit_session_item);
        gtk_widget_show (adata->quit_session_item);
}

static void
destroy_sub_menu (GdmAppletData *adata)
{
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (adata->menuitem), NULL);

        g_signal_handlers_disconnect_by_func (adata->manager,
                                              G_CALLBACK (on_manager_user_added),
                                              adata);
        g_signal_handlers_disconnect_by_func (adata->manager,
                                              G_CALLBACK (on_manager_user_removed),
                                              adata);
}

static void
set_menu_visibility (GdmAppletData *adata,
                     gboolean       visible)
{

        if (visible) {
                create_sub_menu (adata);
        } else {
                destroy_sub_menu (adata);
        }
}

static void
client_notify_lockdown_func (GConfClient   *client,
                             guint          cnxn_id,
                             GConfEntry    *entry,
                             GdmAppletData *adata)
{
        GConfValue *value;
        const char *key;

        value = gconf_entry_get_value (entry);
        key = gconf_entry_get_key (entry);

        if (value == NULL || key == NULL) {
                return;
        }

        if (strcmp (key, LOCKDOWN_KEY) == 0) {
                if (gconf_value_get_bool (value)) {
                        set_menu_visibility (adata, FALSE);
                } else {
                        set_menu_visibility (adata, TRUE);
                }
        }
}

static void
reset_icon (GdmAppletData *adata)
{
        GdkPixbuf *pixbuf;
        GtkWidget *image;

        if (adata->user == NULL || !gtk_widget_has_screen (GTK_WIDGET (adata->menuitem))) {
                return;
        }

        pixbuf = gdm_user_render_icon (adata->user, adata->panel_size);
        if (pixbuf != NULL) {
                image = gtk_image_menu_item_get_image (GTK_IMAGE_MENU_ITEM (adata->menuitem));
                gtk_image_set_from_pixbuf (GTK_IMAGE (image), pixbuf);
                g_object_unref (pixbuf);
        }
}

static void
on_user_icon_changed (GdmUser         *user,
                      GdmAppletData   *adata)
{
        g_debug ("User icon changed");
        reset_icon (adata);
}

/* copied from eel */
static void
_gtk_label_make_bold (GtkLabel *label)
{
        PangoFontDescription *font_desc;

        font_desc = pango_font_description_new ();

        pango_font_description_set_weight (font_desc,
                                           PANGO_WEIGHT_BOLD);

        /* This will only affect the weight of the font, the rest is
         * from the current state of the widget, which comes from the
         * theme or user prefs, since the font desc only has the
         * weight flag turned on.
         */
        gtk_widget_modify_font (GTK_WIDGET (label), font_desc);

        pango_font_description_free (font_desc);
}

static void
setup_current_user (GdmAppletData *adata)
{
        const char *name;
        GtkWidget  *label;

        adata->user = gdm_user_manager_get_user_by_uid (adata->manager, getuid ());
        if (adata->user != NULL) {
                g_object_ref (adata->user);
                name = gdm_user_get_real_name (adata->user);
        } else {
                name = _("Unknown");
        }

        adata->menuitem = gtk_image_menu_item_new_with_label (name);
        label = GTK_BIN (adata->menuitem)->child;
        _gtk_label_make_bold (GTK_LABEL (label));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->menuitem),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menubar), adata->menuitem);
        gtk_widget_show (adata->menuitem);

        if (adata->user != NULL) {
                reset_icon (adata);

                adata->user_icon_changed_id =
                        g_signal_connect (adata->user,
                                          "icon-changed",
                                          G_CALLBACK (on_user_icon_changed),
                                          adata);
                adata->user_notify_id =
                        g_signal_connect (adata->user,
                                         "notify::display-name",
                                         G_CALLBACK (user_notify_display_name_cb),
                                         adata);
        }
}

static gboolean
fill_applet (PanelApplet *applet)
{
        static const BonoboUIVerb menu_verbs[] = {
                BONOBO_UI_VERB ("GdmAboutMe", about_me_cb),
                BONOBO_UI_VERB ("GdmUsersGroupsAdmin", admin_cb),
                BONOBO_UI_VERB ("GdmAbout", about_cb),
                BONOBO_UI_VERB_END
        };
        static gboolean    first_time = FALSE;
        char              *tmp;
        BonoboUIComponent *popup_component;
        GdmAppletData     *adata;

        if (!first_time) {
                first_time = TRUE;

                /* Do this here so it's only done once. */
                gtk_rc_parse_string ("style \"gdm-user-switch-menubar-style\"\n"
                                     "{\n"
                                     "GtkMenuBar::shadow-type = none\n"
                                     "GtkMenuBar::internal-padding = 0\n"
                                     "}\n"
                                     "style \"gdm-user-switch-applet-style\"\n"
                                     "{\n"
                                     "GtkWidget::focus-line-width = 0\n"
                                     "GtkWidget::focus-padding = 0\n"
                                     "}\n"
                                     "widget \"*.gdm-user-switch-menubar\" style \"gdm-user-switch-menubar-style\"\n"
                                     "widget \"*.gdm-user-switch-applet\" style \"gdm-user-switch-applet-style\"\n");
                gtk_window_set_default_icon_name ("stock_people");
                g_set_application_name (_("User Switch Applet"));
        }

        adata = g_new0 (GdmAppletData, 1);
        adata->applet = applet;
        adata->panel_size = 24;

        adata->client = gconf_client_get_default ();

        tmp = g_strdup_printf ("applet-user-menu-item-%p", adata);
        adata->user_menu_item_quark = g_quark_from_string (tmp);
        g_free (tmp);

        if (tooltips == NULL) {
                tooltips = gtk_tooltips_new ();
                g_object_ref (tooltips);
                gtk_object_sink (GTK_OBJECT (tooltips));
        } else {
                g_object_ref (tooltips);
        }

        gtk_tooltips_set_tip (tooltips, GTK_WIDGET (applet), _("User Switcher"), NULL);
        gtk_container_set_border_width (GTK_CONTAINER (applet), 0);
        gtk_widget_set_name (GTK_WIDGET (applet), "gdm-user-switch-applet");
        panel_applet_set_flags (applet, PANEL_APPLET_EXPAND_MINOR);
        panel_applet_setup_menu_from_file (applet, NULL,
                                           DATADIR "/gnome-2.0/ui/GNOME_FastUserSwitchApplet.xml",
                                           NULL, menu_verbs, adata);

        popup_component = panel_applet_get_popup_component (applet);

        /* Hide the admin context menu items if locked down or no cmd-line */
        if (gconf_client_get_bool (adata->client,
                                   "/desktop/gnome/lockdown/inhibit_command_line",
                                   NULL) ||
            panel_applet_get_locked_down (applet)) {
                bonobo_ui_component_set_prop (popup_component,
                                              "/popups/button3/GdmSeparator",
                                              "hidden", "1", NULL);
                bonobo_ui_component_set_prop (popup_component,
                                              "/commands/GdmUsersGroupsAdmin",
                                              "hidden", "1", NULL);
        } else {
#ifndef USERS_ADMIN
#  ifdef GDM_SETUP
                bonobo_ui_component_set_prop (popup_component,
                                              "/popups/button3/GdmSeparator",
                                              "hidden", "1",
                                              NULL);
#  endif /* !GDM_SETUP */
                bonobo_ui_component_set_prop (popup_component,
                                              "/commands/GdmUsersGroupsAdmin",
                                              "hidden", "1",
                                              NULL);
#endif /* !USERS_ADMIN */
        }

        /* Hide the gdmphotosetup item if it can't be found in the path. */
        tmp = g_find_program_in_path ("gnome-about-me");
        if (!tmp) {
                bonobo_ui_component_set_prop (popup_component,
                                              "/commands/GdmAboutMe",
                                              "hidden", "1",
                                              NULL);
        } else {
                g_free (tmp);
        }

        g_signal_connect (adata->applet,
                          "style-set",
                          G_CALLBACK (applet_style_set_cb), adata);
        g_signal_connect (applet,
                          "change-background",
                          G_CALLBACK (applet_change_background_cb), adata);
        g_signal_connect (applet,
                          "size-allocate",
                          G_CALLBACK (applet_size_allocate_cb), adata);
        g_signal_connect (applet,
                          "key-press-event",
                          G_CALLBACK (applet_key_press_event_cb), adata);
        g_signal_connect_after (applet,
                                "focus-in-event",
                                G_CALLBACK (gtk_widget_queue_draw), NULL);
        g_signal_connect_after (applet,
                                "focus-out-event",
                                G_CALLBACK (gtk_widget_queue_draw), NULL);
        g_object_set_data_full (G_OBJECT (applet),
                                "gdm-applet-data",
                                adata,
                                (GDestroyNotify) gdm_applet_data_free);

        adata->menubar = gtk_menu_bar_new ();
        gtk_widget_set_name (adata->menubar, "gdm-user-switch-menubar");
        GTK_WIDGET_SET_FLAGS (adata->menubar,
                              GTK_WIDGET_FLAGS (adata->menubar) | GTK_CAN_FOCUS);
        g_signal_connect (adata->menubar, "button-press-event",
                          G_CALLBACK (menubar_button_press_event_cb), adata);
        g_signal_connect_after (adata->menubar, "expose-event",
                                G_CALLBACK (menubar_expose_event_cb), adata);
        gtk_container_add (GTK_CONTAINER (applet), adata->menubar);
        gtk_widget_show (adata->menubar);

        adata->manager = gdm_user_manager_ref_default ();
        setup_current_user (adata);

        adata->client_notify_lockdown_id = gconf_client_notify_add (adata->client,
                                                                    LOCKDOWN_KEY,
                                                                    (GConfClientNotifyFunc)client_notify_lockdown_func,
                                                                    adata,
                                                                    NULL,
                                                                    NULL);

        if (gconf_client_get_bool (adata->client, LOCKDOWN_KEY, NULL)) {
                set_menu_visibility (adata, FALSE);
        } else {
                set_menu_visibility (adata, TRUE);
        }

        gtk_widget_show (GTK_WIDGET (adata->applet));

        return TRUE;
}

static gboolean
applet_factory (PanelApplet   *applet,
                const char    *iid,
                gpointer       data)
{
        gboolean ret;
        ret = FALSE;
        if (strcmp (iid, "OAFIID:GNOME_FastUserSwitchApplet") == 0) {
                ret = fill_applet (applet);
        }
        return ret;
}
