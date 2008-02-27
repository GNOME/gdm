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
#include <libgnomeui/gnome-help.h>

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

        GtkWidget      *menubar;
        GtkWidget      *imglabel;
        GtkWidget      *menu;
        GtkWidget      *separator_item;
        GtkWidget      *login_screen_item;
        GSList         *items;

        guint           client_notify_lockdown_id;
        guint           user_notify_id;
        GQuark          user_menu_item_quark;
        gint8           pixel_size;
        GtkIconSize     icon_size;
} GdmAppletData;

typedef struct _SelectorResponseData
{
        GdmAppletData  *adata;
        GtkRadioButton *radio;
} SelectorResponseData;

static GtkTooltips *tooltips = NULL;

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
             const char       *cname)
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

static GladeXML *
get_glade_xml (const char *root)
{
        GladeXML *xml;

        xml = glade_xml_new (GLADEDIR "/gdm-user-switch-applet.glade", root, NULL);

        if (xml == NULL) {
                GtkWidget *dialog;

                dialog = gtk_message_dialog_new (NULL,
                                                 0,
                                                 GTK_MESSAGE_ERROR,
                                                 GTK_BUTTONS_CLOSE,
                                                 _("Missing Required File"));
                gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                          _("The User Selector's interfaces file, `%s', could not be opened. It is likely that this application was not properly installed or configured."),
                                                          GLADEDIR "/gdm-user-switch-applet.glade");
                gtk_dialog_run (GTK_DIALOG (dialog));
                gtk_widget_destroy (dialog);
                bonobo_main_quit ();
                return NULL;
        }

        return xml;
}

static void
make_label_bold (GtkLabel *label)
{
        PangoAttrList  *list;
        PangoAttribute *attr;
        gboolean        existing_list;

        list = gtk_label_get_attributes (label);
        existing_list = (list != NULL);
        if (!existing_list) {
                list = pango_attr_list_new ();
        } else {
                pango_attr_list_ref (list);
        }

        attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
        attr->start_index = 0;
        attr->end_index = (guint) -1;
        pango_attr_list_insert (list, attr);

        gtk_label_set_attributes (label, list);
        pango_attr_list_unref (list);
}

static void
make_label_small_italic (GtkLabel *label)
{
        PangoAttrList  *list;
        PangoAttribute *attr;
        gboolean        existing_list;

        list = gtk_label_get_attributes (label);
        existing_list = (list != NULL);
        if (!existing_list) {
                list = pango_attr_list_new ();
        } else {
                pango_attr_list_ref (list);
        }

        attr = pango_attr_style_new (PANGO_STYLE_ITALIC);
        attr->start_index = 0;
        attr->end_index = (guint) -1;
        pango_attr_list_insert (list, attr);

        attr = pango_attr_scale_new (PANGO_SCALE_SMALL);
        attr->start_index = 0;
        attr->end_index = (guint) -1;
        pango_attr_list_insert (list, attr);

        gtk_label_set_attributes (label, list);
        pango_attr_list_unref (list);
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
help_cb (BonoboUIComponent *ui_container,
         GdmAppletData     *adata,
         const char        *cname)
{
        GError *err;

        err = NULL;
        gnome_help_display_on_screen ("gdm-user-switch-applet", NULL,
                                      gtk_widget_get_screen (GTK_WIDGET (adata->applet)),
                                      &err);
        if (err != NULL) {
                g_warning ("Could not open help document: %s", err->message);
                g_error_free (err);
        }
}

static void
about_cb (BonoboUIComponent *ui_container,
          gpointer           data,
          const char       *cname)
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
          const char       *cname)
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
        GdkScreen *screen;
        GtkIconTheme *theme;
        gint width, height, icon_size;

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
                             gpointer                   data)
{
        GdmAppletData *adata;
        GtkRcStyle    *rc_style;
        GtkStyle      *style;

        adata = data;

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
                if (style->bg_pixmap[GTK_STATE_NORMAL])
                        g_object_unref (style->bg_pixmap[GTK_STATE_NORMAL]);

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
applet_key_press_event_cb (GtkWidget   *widget,
                           GdkEventKey *event,
                           gpointer     data)
{
        GdmAppletData *adata;
        GtkMenuShell *menu_shell;

        switch (event->keyval) {
        case GDK_KP_Enter:
        case GDK_ISO_Enter:
        case GDK_3270_Enter:
        case GDK_Return:
        case GDK_space:
        case GDK_KP_Space:
                adata = data;
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
                         gpointer       data)
{
        GdmAppletData *adata;
        GList *children;
        GtkWidget *top_item;
        PanelAppletOrient orient;
        gint item_border;
        gint pixel_size;
        gdouble text_angle = 0.0;
        GtkPackDirection pack_direction = GTK_PACK_DIRECTION_LTR;

        adata = data;
        children = gtk_container_get_children (GTK_CONTAINER (adata->menubar));
        top_item = GTK_WIDGET (children->data);
        g_list_free (children);

        orient = panel_applet_get_orient (PANEL_APPLET (widget));

        item_border = (MAX (top_item->style->xthickness,
                            top_item->style->ythickness) * 2);

        switch (orient) {
        case PANEL_APPLET_ORIENT_UP:
        case PANEL_APPLET_ORIENT_DOWN:
                gtk_widget_set_size_request (top_item, -1, allocation->height);
                pixel_size = allocation->height - item_border;
                pack_direction = GTK_PACK_DIRECTION_RTL;
                text_angle = 0.0;
                break;
        case PANEL_APPLET_ORIENT_LEFT:
                gtk_widget_set_size_request (top_item, allocation->width, -1);
                pixel_size = allocation->width - item_border;
                pack_direction = GTK_PACK_DIRECTION_TTB;
                text_angle = 270.0;
                break;
        case PANEL_APPLET_ORIENT_RIGHT:
                gtk_widget_set_size_request (top_item, allocation->width, -1);
                pixel_size = allocation->width - item_border;
                pack_direction = GTK_PACK_DIRECTION_TTB;
                text_angle = 90.0;
                break;
        default:
                g_assert_not_reached ();
                break;
        }

        gtk_menu_bar_set_pack_direction (GTK_MENU_BAR (adata->menubar),
                                         pack_direction);

        if (GTK_IS_IMAGE (adata->imglabel)) {
                gtk_image_set_pixel_size (GTK_IMAGE (adata->imglabel),
                                          pixel_size - 4);
        } else {
                gtk_label_set_angle (GTK_LABEL (adata->imglabel), text_angle);
        }
}


static void
gdm_applet_data_free (GdmAppletData *adata)
{
        gconf_client_notify_remove (adata->client, adata->client_notify_lockdown_id);

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
                         gpointer        data)
{
        GdmAppletData *adata;

        adata = data;

        if (GTK_WIDGET_HAS_FOCUS (adata->applet))
                gtk_paint_focus (widget->style, widget->window, GTK_WIDGET_STATE (widget),
                                 NULL, widget, "menu-applet", 0, 0, -1, -1);

        return FALSE;
}

static gint
sort_menu_comparedatafunc (gconstpointer a,
                           gconstpointer b,
                           gpointer      data)
{
        GdmAppletData *adata;
        gboolean a_is_user, b_is_user;

        a_is_user = GDM_IS_USER_MENU_ITEM (a);
        b_is_user = GDM_IS_USER_MENU_ITEM (b);

        if (a_is_user && !b_is_user)
                return -1;

        if (b_is_user && !a_is_user)
                return 1;

        if (a_is_user && b_is_user)
                return gdm_user_collate (gdm_user_menu_item_get_user ((GdmUserMenuItem *) a),
                                         gdm_user_menu_item_get_user ((GdmUserMenuItem *) b));

        adata = data;
        if (a == adata->separator_item)
                return -1;

        if (b == adata->separator_item)
                return 1;

        if (a == adata->login_screen_item)
                return -1;

        if (b == adata->login_screen_item)
                return 1;

        return 0;
}

static void
sort_menu (GdmAppletData *adata)
{
        GSList *items;
        guint n_items, n_rows, n_cols, row, column, count;
        gint screen_height;

        if (!gtk_widget_has_screen (adata->menu)) {
                return;
        }

        adata->items = g_slist_sort_with_data (adata->items,
                                               sort_menu_comparedatafunc, adata);

        screen_height = gdk_screen_get_height (gtk_widget_get_screen (adata->menu));

        n_items = 0;
        items = adata->items;
        while (items) {
                if (GTK_WIDGET_VISIBLE (items->data))
                        n_items++;

                items = items->next;
        }

        /* FIXME: Do a better job of figuring out exactly how big the menuitems are */
        n_rows = (gdouble) screen_height / (gdouble) (adata->pixel_size + 16);
        n_cols = (gdouble) n_items / (gdouble) n_rows;
        n_rows = (gdouble) n_items / (gdouble) (n_cols + 1);

        row = 0;
        column = 0;
        count = 0;
        items = adata->items;
        while (items) {
                if (GTK_WIDGET_VISIBLE (items->data)) {
                        gtk_menu_attach (GTK_MENU (adata->menu), items->data,
                                         column, column + 1, row, row + 1);
                        row++;
                        if (row > n_rows) {
                                row = 0;
                                column++;
                        }

                        /* Just re-attaching them doesn't alter the order you get them
                         * in when you move through with the arrow keys, though; we
                         * have to set that explicitly.
                         */
                        gtk_menu_reorder_child (GTK_MENU (adata->menu),
                                                items->data, count++);
                }

                items = items->next;
        }
}

static void
menu_style_set_cb (GtkWidget *menu,
                   GtkStyle  *old_style,
                   gpointer   data)
{
        GdmAppletData *adata;
        GtkSettings *settings;
        gint width, height;

        adata = data;
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
                                                &width, &height))
                adata->pixel_size = -1;
        else
                adata->pixel_size = MAX (width, height);

        sort_menu (adata);
}

static void
menuitem_destroy_cb (GtkWidget *menuitem,
                     gpointer   data)
{
        GdmAppletData *adata;
        GSList *li;

        adata = data;

        if (GDM_IS_USER_MENU_ITEM (menuitem)) {
                GdmUser *user;

                user = gdm_user_menu_item_get_user (GDM_USER_MENU_ITEM (menuitem));
                if (user != NULL) {
                        g_object_set_qdata (G_OBJECT (user),
                                            adata->user_menu_item_quark, NULL);
                }
        }

        li = g_slist_find (adata->items, menuitem);
        adata->items = g_slist_delete_link (adata->items, li);
        sort_menu (adata);
}

static void
menuitem_style_set_cb (GtkWidget *menuitem,
                       GtkStyle  *old_style,
                       gpointer   data)
{
        GdmAppletData *adata;

        adata = data;

        if (GDM_IS_USER_MENU_ITEM (menuitem)) {
                gdm_user_menu_item_set_icon_size (GDM_USER_MENU_ITEM (menuitem),
                                                   adata->pixel_size);
        } else {
                GtkWidget *image;
                const char *icon_name;

                if (menuitem == adata->login_screen_item) {
                        icon_name = "gdm";
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
user_notify_display_name_cb (GObject    *object,
                             GParamSpec *pspec,
                             gpointer    data)
{
        gtk_label_set_text (data, gdm_user_get_real_name (GDM_USER (object)));
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
switch_to_user_session (GdmAppletData *adata,
                        GdmUser       *user)
{
        gdm_user_manager_activate_user_session (adata->manager, user);
}

static void
maybe_lock_screen (GdmAppletData *adata)
{
        char      *args[3];
        GError    *err;
        GdkScreen *screen;
        gboolean   use_gscreensaver = TRUE;

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
        if (!gdk_spawn_on_screen (screen, g_get_home_dir (), args, NULL,
                                  (G_SPAWN_STDERR_TO_DEV_NULL |
                                   G_SPAWN_STDOUT_TO_DEV_NULL),
                                  NULL, NULL, NULL, &err)) {
                g_warning (_("Can't lock screen: %s"), err->message);
                g_error_free (err);
        }

        if (use_gscreensaver) {
                args[1] = "--throttle";
        } else {
                args[1] = "-throttle";
        }

        if (!gdk_spawn_on_screen (screen, g_get_home_dir (), args, NULL,
                                  (G_SPAWN_STDERR_TO_DEV_NULL |
                                   G_SPAWN_STDOUT_TO_DEV_NULL),
                                  NULL, NULL, NULL, &err)) {
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
                return;
        }

        num_sessions = gdm_user_get_num_sessions (user);
        if (num_sessions > 0) {
                switch_to_user_session (adata, user);
        } else {
                gdm_user_manager_goto_login_session (adata->manager);
        }

        maybe_lock_screen (adata);
}

static void
user_item_activate_cb (GtkWidget     *menuitem,
                       GdmAppletData *adata)
{
        GdmUserMenuItem *item;
        GdmUser         *user;

        item = GDM_USER_MENU_ITEM (menuitem);
        user = gdm_user_menu_item_get_user (item);

        do_switch (adata, user);
}

static void
user_sessions_changed_cb (GdmUser       *user,
                          GdmAppletData *adata)
{
        GtkWidget *menuitem;

        menuitem = g_object_get_qdata (G_OBJECT (user), adata->user_menu_item_quark);
        if (menuitem == NULL) {
                return;
        }

        gtk_widget_show (menuitem);

        sort_menu (adata);
}

static void
add_user (GdmAppletData  *adata,
          GdmUser        *user)
{
        GtkWidget *menuitem;

        menuitem = gdm_user_menu_item_new (user);
        g_object_set_qdata (G_OBJECT (user), adata->user_menu_item_quark, menuitem);
        g_signal_connect (menuitem,
                          "style-set",
                          G_CALLBACK (menuitem_style_set_cb),
                          adata);
        g_signal_connect (menuitem,
                          "destroy",
                          G_CALLBACK (menuitem_destroy_cb),
                          adata);
        g_signal_connect (menuitem,
                          "activate",
                          G_CALLBACK (user_item_activate_cb),
                          adata);
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu), menuitem);
        adata->items = g_slist_prepend (adata->items, menuitem);

        gtk_widget_show (menuitem);

        g_signal_connect (user,
                          "sessions-changed",
                          G_CALLBACK (user_sessions_changed_cb),
                          adata);
}

static void
manager_user_added_cb (GdmUserManager *manager,
                       GdmUser        *user,
                       GdmAppletData  *adata)
{
        add_user (adata, user);
        sort_menu (adata);
}

static void
login_screen_activate_cb (GtkMenuItem *item,
                          gpointer     data)
{
        GdmAppletData *adata;
        GdmUser       *user;

        adata = data;
        user = NULL;

        do_switch (adata, user);
}

static void
client_notify_applet_func (GConfClient   *client,
                           guint          cnxn_id,
                           GConfEntry    *entry,
                           GdmAppletData *adata)
{
        GConfValue *value;
        char       *key;

        value = gconf_entry_get_value (entry);

        if (value == NULL)
                return;

        key = g_path_get_basename (gconf_entry_get_key (entry));

        if (key == NULL)
                return;

        g_free (key);
}


static void
client_notify_global_func (GConfClient   *client,
                           guint          cnxn_id,
                           GConfEntry    *entry,
                           GdmAppletData *adata)
{
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

        if (!value || !key)
                return;

        if (strcmp (key, LOCKDOWN_KEY) == 0) {
                if (gconf_value_get_bool (value)) {
                        gtk_widget_hide ( GTK_WIDGET (adata->applet));
                } else {
                        gtk_widget_show ( GTK_WIDGET (adata->applet));
                }
        }
}

static gboolean
fill_applet (PanelApplet *applet)
{
        static const BonoboUIVerb menu_verbs[] = {
                BONOBO_UI_VERB ("GdmAboutMe", about_me_cb),
                BONOBO_UI_VERB ("GdmUsersGroupsAdmin", admin_cb),
                BONOBO_UI_VERB ("GdmHelp", (BonoboUIVerbFn)help_cb),
                BONOBO_UI_VERB ("GdmAbout", about_cb),
                BONOBO_UI_VERB_END
        };
        static gboolean    first_time = FALSE;
        GtkWidget         *menuitem;
        GtkWidget         *hbox;
        GSList            *users;
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

        adata->client = gconf_client_get_default ();
        adata->manager = gdm_user_manager_ref_default ();

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

        menuitem = gtk_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menubar), menuitem);
        gtk_widget_show (menuitem);

        hbox = gtk_hbox_new (FALSE, 0);
        gtk_container_add (GTK_CONTAINER (menuitem), hbox);
        gtk_widget_show (hbox);

        {
                GdmUser *user;

                user = gdm_user_manager_get_user_by_uid (adata->manager, getuid ());
                adata->imglabel = gtk_label_new (gdm_user_get_real_name (user));
                adata->user_notify_id = g_signal_connect (user,
                                                          "notify::display-name",
                                                          G_CALLBACK (user_notify_display_name_cb),
                                                          adata->imglabel);
                gtk_box_pack_start (GTK_BOX (hbox), adata->imglabel, TRUE, TRUE, 0);
                gtk_widget_show (adata->imglabel);
        }

        adata->menu = gtk_menu_new ();
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), adata->menu);
        g_signal_connect (adata->menu, "style-set",
                          G_CALLBACK (menu_style_set_cb), adata);
        g_signal_connect (adata->menu, "show",
                          G_CALLBACK (menu_expose_cb), adata);
        gtk_widget_show (adata->menu);

        /* This next part populates the list with all the users we currently know
         * about. For almost all cases, this is the empty list, because we're
         * asynchronous, and the data hasn't come back from the callback saying who
         * the users are yet. However, if someone has two GDMs on their toolbars (why,
         * I have no freaking idea, but bear with me here), the menu of the second
         * one to be initialised needs to be filled in from the start rather than
         * depending on getting data from the callback like the first one.
         */
        users = gdm_user_manager_list_users (adata->manager);
        while (users != NULL) {
                add_user (adata, users->data);

                users = g_slist_delete_link (users, users);
        }

        g_signal_connect (adata->manager,
                          "user-added",
                          G_CALLBACK (manager_user_added_cb),
                          adata);

        adata->separator_item = gtk_separator_menu_item_new ();
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu), adata->separator_item);
        adata->items = g_slist_prepend (adata->items, adata->separator_item);
        gtk_widget_show (adata->separator_item);

        adata->login_screen_item = gtk_image_menu_item_new_with_label (_("Other..."));
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (adata->login_screen_item),
                                       gtk_image_new ());
        gtk_menu_shell_append (GTK_MENU_SHELL (adata->menu),
                               adata->login_screen_item);
        g_signal_connect (adata->login_screen_item, "style-set",
                          G_CALLBACK (menuitem_style_set_cb), adata);
        g_signal_connect (adata->login_screen_item, "destroy",
                          G_CALLBACK (menuitem_destroy_cb), adata);
        g_signal_connect (adata->login_screen_item, "activate",
                          G_CALLBACK (login_screen_activate_cb), adata);
        adata->items = g_slist_prepend (adata->items, adata->login_screen_item);
        gtk_widget_show (adata->login_screen_item);

        adata->client_notify_lockdown_id = gconf_client_notify_add (adata->client,
                                                                    LOCKDOWN_KEY,
                                                                    (GConfClientNotifyFunc)client_notify_lockdown_func,
                                                                    adata,
                                                                    NULL,
                                                                    NULL);

        adata->items = g_slist_sort_with_data (adata->items,
                                               sort_menu_comparedatafunc, adata);

        if (gconf_client_get_bool (adata->client, LOCKDOWN_KEY, NULL)) {
                gtk_widget_hide (GTK_WIDGET (applet));
        } else {
                gtk_widget_show (GTK_WIDGET (applet));
        }

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
