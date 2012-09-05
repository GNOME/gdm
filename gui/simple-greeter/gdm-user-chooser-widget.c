/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2007 Ray Strode <rstrode@redhat.com>
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
#include <errno.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <act/act-user-manager.h>
#include <act/act-user.h>

#include "gdm-user-chooser-widget.h"
#include "gdm-settings-keys.h"
#include "gdm-settings-client.h"

#define LOGIN_SCREEN_SCHEMA   "org.gnome.login-screen"

#define KEY_DISABLE_USER_LIST "disable-user-list"

enum {
        USER_NO_DISPLAY              = 1 << 0,
        USER_ACCOUNT_DISABLED        = 1 << 1,
};

#define DEFAULT_USER_ICON "avatar-default"
#define OLD_DEFAULT_USER_ICON "stock_person"

#define GDM_USER_CHOOSER_WIDGET_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_USER_CHOOSER_WIDGET, GdmUserChooserWidgetPrivate))

#define MAX_ICON_SIZE 128
#define NUM_USERS_TO_ADD_PER_ITERATION 50

struct GdmUserChooserWidgetPrivate
{
        ActUserManager *manager;
        GtkIconTheme   *icon_theme;

        GSList         *users_to_add;

        GdkPixbuf      *logged_in_pixbuf;
        GdkPixbuf      *stock_person_pixbuf;

        guint           loaded : 1;
        guint           show_user_other : 1;
        guint           show_user_guest : 1;
        guint           show_user_auto : 1;
        guint           show_normal_users : 1;

        guint           has_user_other : 1;

        guint           update_other_visibility_idle_id;
        guint           load_idle_id;
        guint           add_users_idle_id;
};

enum {
        PROP_0,
        PROP_SHOW_USER_GUEST,
        PROP_SHOW_USER_AUTO,
        PROP_SHOW_USER_OTHER,
};

static void     gdm_user_chooser_widget_class_init  (GdmUserChooserWidgetClass *klass);
static void     gdm_user_chooser_widget_init        (GdmUserChooserWidget      *user_chooser_widget);
static void     gdm_user_chooser_widget_finalize    (GObject                   *object);

G_DEFINE_TYPE (GdmUserChooserWidget, gdm_user_chooser_widget, GDM_TYPE_CHOOSER_WIDGET)

static void     add_user_other    (GdmUserChooserWidget *widget);
static void     remove_user_other (GdmUserChooserWidget *widget);

static int
get_font_height_for_widget (GtkWidget *widget)
{
        PangoFontMetrics *metrics;
        PangoContext     *context;
        int               ascent;
        int               descent;
        int               height;

        gtk_widget_ensure_style (widget);
        context = gtk_widget_get_pango_context (widget);
        metrics = pango_context_get_metrics (context,
                                             gtk_widget_get_style (widget)->font_desc,
                                             pango_context_get_language (context));

        ascent = pango_font_metrics_get_ascent (metrics);
        descent = pango_font_metrics_get_descent (metrics);
        height = PANGO_PIXELS (ascent + descent);
        pango_font_metrics_unref (metrics);
        return height;
}

static int
get_icon_height_for_widget (GtkWidget *widget)
{
        int font_height;
        int height;

        font_height = get_font_height_for_widget (widget);
        height = 3 * font_height;
        if (height > MAX_ICON_SIZE) {
                height = MAX_ICON_SIZE;
        }

        g_debug ("GdmUserChooserWidget: font height %d; using icon size %d", font_height, height);

        return height;
}

static gboolean
update_other_user_visibility (GdmUserChooserWidget *widget)
{
        g_debug ("GdmUserChooserWidget: updating other user visibility");

        if (!widget->priv->show_user_other) {
                if (widget->priv->has_user_other) {
                        remove_user_other (widget);
                }

                goto out;
        }

        /* Always show the Other user if requested */
        if (!widget->priv->has_user_other) {
                add_user_other (widget);
        }

 out:
        widget->priv->update_other_visibility_idle_id = 0;
        return FALSE;
}

static void
queue_update_other_user_visibility (GdmUserChooserWidget *widget)
{
        if (widget->priv->update_other_visibility_idle_id == 0) {
                widget->priv->update_other_visibility_idle_id =
                        g_idle_add ((GSourceFunc) update_other_user_visibility, widget);
        }
}

static void
rounded_rectangle (cairo_t *cr,
                   gdouble  aspect,
                   gdouble  x,
                   gdouble  y,
                   gdouble  corner_radius,
                   gdouble  width,
                   gdouble  height)
{
        gdouble radius;
        gdouble degrees;

        radius = corner_radius / aspect;
        degrees = G_PI / 180.0;

        cairo_new_sub_path (cr);
        cairo_arc (cr,
                   x + width - radius,
                   y + radius,
                   radius,
                   -90 * degrees,
                   0 * degrees);
        cairo_arc (cr,
                   x + width - radius,
                   y + height - radius,
                   radius,
                   0 * degrees,
                   90 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + height - radius,
                   radius,
                   90 * degrees,
                   180 * degrees);
        cairo_arc (cr,
                   x + radius,
                   y + radius,
                   radius,
                   180 * degrees,
                   270 * degrees);
        cairo_close_path (cr);
}

static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf *pixbuf)
{
        cairo_surface_t *surface;
        cairo_t         *cr;

        surface = cairo_image_surface_create (gdk_pixbuf_get_has_alpha (pixbuf) ?
                                              CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
                                              gdk_pixbuf_get_width (pixbuf),
                                              gdk_pixbuf_get_height (pixbuf));
        cr = cairo_create (surface);
        gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
        cairo_paint (cr);
        cairo_destroy (cr);

        return surface;
}

/**
 * go_cairo_convert_data_to_pixbuf:
 * @src: a pointer to pixel data in cairo format
 * @dst: a pointer to pixel data in pixbuf format
 * @width: image width
 * @height: image height
 * @rowstride: data rowstride
 *
 * Converts the pixel data stored in @src in CAIRO_FORMAT_ARGB32 cairo format
 * to GDK_COLORSPACE_RGB pixbuf format and move them
 * to @dst. If @src == @dst, pixel are converted in place.
 **/

static void
go_cairo_convert_data_to_pixbuf (unsigned char *dst,
                                 unsigned char const *src,
                                 int width,
                                 int height,
                                 int rowstride)
{
        int i,j;
        unsigned int t;
        unsigned char a, b, c;

        g_return_if_fail (dst != NULL);

#define MULT(d,c,a,t) G_STMT_START { t = (a)? c * 255 / a: 0; d = t;} G_STMT_END

        if (src == dst || src == NULL) {
                for (i = 0; i < height; i++) {
                        for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                MULT(a, dst[2], dst[3], t);
                                MULT(b, dst[1], dst[3], t);
                                MULT(c, dst[0], dst[3], t);
                                dst[0] = a;
                                dst[1] = b;
                                dst[2] = c;
#else
                                MULT(a, dst[1], dst[0], t);
                                MULT(b, dst[2], dst[0], t);
                                MULT(c, dst[3], dst[0], t);
                                dst[3] = dst[0];
                                dst[0] = a;
                                dst[1] = b;
                                dst[2] = c;
#endif
                                dst += 4;
                        }
                        dst += rowstride - width * 4;
                }
        } else {
                for (i = 0; i < height; i++) {
                        for (j = 0; j < width; j++) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
                                MULT(dst[0], src[2], src[3], t);
                                MULT(dst[1], src[1], src[3], t);
                                MULT(dst[2], src[0], src[3], t);
                                dst[3] = src[3];
#else
                                MULT(dst[0], src[1], src[0], t);
                                MULT(dst[1], src[2], src[0], t);
                                MULT(dst[2], src[3], src[0], t);
                                dst[3] = src[0];
#endif
                                src += 4;
                                dst += 4;
                        }
                        src += rowstride - width * 4;
                        dst += rowstride - width * 4;
                }
        }
#undef MULT
}

static void
cairo_to_pixbuf (guint8    *src_data,
                 GdkPixbuf *dst_pixbuf)
{
        unsigned char *src;
        unsigned char *dst;
        guint          w;
        guint          h;
        guint          rowstride;

        w = gdk_pixbuf_get_width (dst_pixbuf);
        h = gdk_pixbuf_get_height (dst_pixbuf);
        rowstride = gdk_pixbuf_get_rowstride (dst_pixbuf);

        dst = gdk_pixbuf_get_pixels (dst_pixbuf);
        src = src_data;

        go_cairo_convert_data_to_pixbuf (dst, src, w, h, rowstride);
}

static GdkPixbuf *
frame_pixbuf (GdkPixbuf *source)
{
        GdkPixbuf       *dest;
        cairo_t         *cr;
        cairo_surface_t *surface;
        guint            w;
        guint            h;
        guint            rowstride;
        int              frame_width;
        double           radius;
        guint8          *data;

        frame_width = 2;

        w = gdk_pixbuf_get_width (source) + frame_width * 2;
        h = gdk_pixbuf_get_height (source) + frame_width * 2;
        radius = w / 10;

        dest = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
                               TRUE,
                               8,
                               w,
                               h);
        rowstride = gdk_pixbuf_get_rowstride (dest);


        data = g_new0 (guint8, h * rowstride);

        surface = cairo_image_surface_create_for_data (data,
                                                       CAIRO_FORMAT_ARGB32,
                                                       w,
                                                       h,
                                                       rowstride);
        cr = cairo_create (surface);
        cairo_surface_destroy (surface);

        /* set up image */
        cairo_rectangle (cr, 0, 0, w, h);
        cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.0);
        cairo_fill (cr);

        rounded_rectangle (cr,
                           1.0,
                           frame_width + 0.5,
                           frame_width + 0.5,
                           radius,
                           w - frame_width * 2 - 1,
                           h - frame_width * 2 - 1);
        cairo_set_source_rgba (cr, 0.5, 0.5, 0.5, 0.3);
        cairo_fill_preserve (cr);

        surface = surface_from_pixbuf (source);
        cairo_set_source_surface (cr, surface, frame_width, frame_width);
        cairo_fill (cr);
        cairo_surface_destroy (surface);

        cairo_to_pixbuf (data, dest);

        cairo_destroy (cr);
        g_free (data);

        return dest;
}

static GdkPixbuf *
render_user_icon (GdmUserChooserWidget *widget,
                  ActUser              *user)
{
        int size;
        const char *file;
        GdkPixbuf *pixbuf;
        GdkPixbuf *framed;

        pixbuf = NULL;

        size = get_icon_height_for_widget (GTK_WIDGET (widget));
        file = act_user_get_icon_file (user);

        if (file) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (file, size, size, NULL);
        }

        if (pixbuf == NULL) {
                GError *error;

                error = NULL;
                pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                                   "avatar-default",
                                                   size,
                                                   GTK_ICON_LOOKUP_FORCE_SIZE,
                                                   &error);
                if (error != NULL) {
                        g_warning ("%s", error->message);
                        g_error_free (error);
                }
        }

        if (pixbuf != NULL) {
                framed = frame_pixbuf (pixbuf);
                g_object_unref (pixbuf);

                pixbuf = framed;
        }

        return pixbuf;
}

static void
update_item_for_user (GdmUserChooserWidget *widget,
                      ActUser              *user)
{
        GdkPixbuf    *pixbuf;
        char         *tooltip;
        gboolean      is_logged_in;
        char         *escaped_username;
        const char   *real_name;
        char         *escaped_real_name;

        if (!act_user_is_loaded (user)) {
                return;
        }

        pixbuf = render_user_icon (widget, user);

        if (pixbuf == NULL && widget->priv->stock_person_pixbuf != NULL) {
                pixbuf = g_object_ref (widget->priv->stock_person_pixbuf);
        }

        tooltip = g_strdup_printf (_("Log in as %s"),
                                   act_user_get_user_name (user));

        is_logged_in = act_user_is_logged_in (user);

        g_debug ("GdmUserChooserWidget: User added name:%s logged-in:%d pixbuf:%p",
                 act_user_get_user_name (user),
                 is_logged_in,
                 pixbuf);

        escaped_username = g_markup_escape_text (act_user_get_user_name (user), -1);

        real_name = act_user_get_real_name (user);
        if (real_name == NULL || real_name[0] == '\0') {
                real_name = act_user_get_user_name (user);
        }
        escaped_real_name = g_markup_escape_text (real_name, -1);

        /* Ignore updates we aren't interested in */
        if (!gdm_chooser_widget_lookup_item (GDM_CHOOSER_WIDGET (widget),
                                             escaped_username,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL)) {
                return;
        }

        gdm_chooser_widget_update_item (GDM_CHOOSER_WIDGET (widget),
                                        escaped_username,
                                        pixbuf,
                                        escaped_real_name,
                                        tooltip,
                                        act_user_get_login_frequency (user),
                                        is_logged_in,
                                        FALSE);
        g_free (escaped_real_name);
        g_free (escaped_username);
        g_free (tooltip);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }
}

static void
on_item_load (GdmChooserWidget     *widget,
              const char           *id,
              GdmUserChooserWidget *user_chooser)
{
        ActUser *user;

        g_debug ("GdmUserChooserWidget: Loading item for id=%s", id);

        if (user_chooser->priv->manager == NULL) {
                return;
        }

        if (strcmp (id, GDM_USER_CHOOSER_USER_OTHER) == 0) {
                return;
        }

        if (strcmp (id, GDM_USER_CHOOSER_USER_GUEST) == 0) {
                return;
        }

        user = act_user_manager_get_user (user_chooser->priv->manager, id);
        if (user != NULL) {
                update_item_for_user (user_chooser, user);
        }
}

static void
add_user_other (GdmUserChooserWidget *widget)
{
        widget->priv->has_user_other = TRUE;
        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_USER_CHOOSER_USER_OTHER,
                                     NULL,
                                     /* translators: This option prompts
                                      * the user to type in a username
                                      * manually instead of choosing from
                                      * a list.
                                      */
                                     C_("user", "Other…"),
                                     _("Choose a different account"),
                                     0,
                                     FALSE,
                                     TRUE,
                                     (GdmChooserWidgetItemLoadFunc) on_item_load,
                                     widget);
}

static void
add_user_guest (GdmUserChooserWidget *widget)
{
        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_USER_CHOOSER_USER_GUEST,
                                     widget->priv->stock_person_pixbuf,
                                     _("Guest"),
                                     _("Log in as a temporary guest"),
                                     0,
                                     FALSE,
                                     TRUE,
                                     (GdmChooserWidgetItemLoadFunc) on_item_load,
                                     widget);
        queue_update_other_user_visibility (widget);
}

static void
add_user_auto (GdmUserChooserWidget *widget)
{
        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     GDM_USER_CHOOSER_USER_AUTO,
                                     NULL,
                                     _("Automatic Login"),
                                     _("Automatically log into the system after selecting options"),
                                     0,
                                     FALSE,
                                     TRUE,
                                     (GdmChooserWidgetItemLoadFunc) on_item_load,
                                     widget);
        queue_update_other_user_visibility (widget);
}

static void
remove_user_other (GdmUserChooserWidget *widget)
{
        widget->priv->has_user_other = FALSE;
        gdm_chooser_widget_remove_item (GDM_CHOOSER_WIDGET (widget),
                                        GDM_USER_CHOOSER_USER_OTHER);
}

static void
remove_user_guest (GdmUserChooserWidget *widget)
{
        gdm_chooser_widget_remove_item (GDM_CHOOSER_WIDGET (widget),
                                        GDM_USER_CHOOSER_USER_GUEST);
        queue_update_other_user_visibility (widget);
}

static void
remove_user_auto (GdmUserChooserWidget *widget)
{
        gdm_chooser_widget_remove_item (GDM_CHOOSER_WIDGET (widget),
                                        GDM_USER_CHOOSER_USER_AUTO);
        queue_update_other_user_visibility (widget);
}

void
gdm_user_chooser_widget_set_show_user_other (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_user_other != show_user) {
                widget->priv->show_user_other = show_user;
                queue_update_other_user_visibility (widget);
        }
}

void
gdm_user_chooser_widget_set_show_user_guest (GdmUserChooserWidget *widget,
                                             gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_user_guest != show_user) {
                widget->priv->show_user_guest = show_user;
                if (show_user) {
                        add_user_guest (widget);
                } else {
                        remove_user_guest (widget);
                }
        }
}

void
gdm_user_chooser_widget_set_show_user_auto (GdmUserChooserWidget *widget,
                                            gboolean              show_user)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        if (widget->priv->show_user_auto != show_user) {
                widget->priv->show_user_auto = show_user;
                if (show_user) {
                        add_user_auto (widget);
                } else {
                        remove_user_auto (widget);
                }
        }
}

char *
gdm_user_chooser_widget_get_chosen_user_name (GdmUserChooserWidget *widget)
{
        char *active_item_id;
        gboolean isnt_user;

        g_return_val_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget), NULL);

        active_item_id = gdm_chooser_widget_get_active_item (GDM_CHOOSER_WIDGET (widget));
        if (active_item_id == NULL) {
                g_debug ("GdmUserChooserWidget: no active item in list");
                return NULL;
        }

        gdm_chooser_widget_lookup_item (GDM_CHOOSER_WIDGET (widget), active_item_id,
                                        NULL, NULL, NULL, NULL, NULL,
                                        &isnt_user);

        if (isnt_user) {
                g_debug ("GdmUserChooserWidget: active item '%s' isn't a user", active_item_id);
                g_free (active_item_id);
                return NULL;
        }

        g_debug ("GdmUserChooserWidget: active item '%s' is a user", active_item_id);

        return active_item_id;
}

void
gdm_user_chooser_widget_set_chosen_user_name (GdmUserChooserWidget *widget,
                                              const char           *name)
{
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_active_item (GDM_CHOOSER_WIDGET (widget), name);
}

void
gdm_user_chooser_widget_set_show_only_chosen (GdmUserChooserWidget *widget,
                                              gboolean              show_only) {
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (widget));

        gdm_chooser_widget_set_hide_inactive_items (GDM_CHOOSER_WIDGET (widget),
                                                    show_only);

}
static void
gdm_user_chooser_widget_set_property (GObject        *object,
                                      guint           prop_id,
                                      const GValue   *value,
                                      GParamSpec     *pspec)
{
        GdmUserChooserWidget *self;

        self = GDM_USER_CHOOSER_WIDGET (object);

        switch (prop_id) {
        case PROP_SHOW_USER_AUTO:
                gdm_user_chooser_widget_set_show_user_auto (self, g_value_get_boolean (value));
                break;
        case PROP_SHOW_USER_GUEST:
                gdm_user_chooser_widget_set_show_user_guest (self, g_value_get_boolean (value));
                break;
        case PROP_SHOW_USER_OTHER:
                gdm_user_chooser_widget_set_show_user_other (self, g_value_get_boolean (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_user_chooser_widget_get_property (GObject        *object,
                                      guint           prop_id,
                                      GValue         *value,
                                      GParamSpec     *pspec)
{
        GdmUserChooserWidget *self;

        self = GDM_USER_CHOOSER_WIDGET (object);

        switch (prop_id) {
        case PROP_SHOW_USER_AUTO:
                g_value_set_boolean (value, self->priv->show_user_auto);
                break;
        case PROP_SHOW_USER_GUEST:
                g_value_set_boolean (value, self->priv->show_user_guest);
                break;
        case PROP_SHOW_USER_OTHER:
                g_value_set_boolean (value, self->priv->show_user_other);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
is_user_list_disabled (GdmUserChooserWidget *widget)
{
        GSettings *settings;
        gboolean   result;

        settings = g_settings_new (LOGIN_SCREEN_SCHEMA);
        result = g_settings_get_boolean (settings, KEY_DISABLE_USER_LIST);
        g_object_unref (settings);

        return result;
}

static void
add_user (GdmUserChooserWidget *widget,
          ActUser              *user)
{
        GdkPixbuf    *pixbuf;
        char         *tooltip;
        gboolean      is_logged_in;
        char         *escaped_username;
        const char   *real_name;
        char         *escaped_real_name;

        if (!widget->priv->show_normal_users) {
                return;
        }

        if (strcmp (act_user_get_user_name (user), GDM_USERNAME) == 0) {
                return;
        }

        if (act_user_get_uid (user) == 0) {
                return;
        }

        if (act_user_get_locked (user)) {
                g_debug ("GdmUserChooserWidget: Skipping locked user: %s", act_user_get_user_name (user));
                return;
        }

        g_debug ("GdmUserChooserWidget: User added: %s", act_user_get_user_name (user));
        if (widget->priv->stock_person_pixbuf != NULL) {
                pixbuf = g_object_ref (widget->priv->stock_person_pixbuf);
        } else {
                pixbuf = NULL;
        }

        tooltip = g_strdup_printf (_("Log in as %s"),
                                   act_user_get_user_name (user));

        is_logged_in = act_user_is_logged_in (user);

        escaped_username = g_markup_escape_text (act_user_get_user_name (user), -1);
        real_name = act_user_get_real_name (user);
        if (real_name == NULL || real_name[0] == '\0') {
                real_name = act_user_get_user_name (user);
        }
        escaped_real_name = g_markup_escape_text (real_name, -1);

        gdm_chooser_widget_add_item (GDM_CHOOSER_WIDGET (widget),
                                     escaped_username,
                                     pixbuf,
                                     escaped_real_name,
                                     tooltip,
                                     act_user_get_login_frequency (user),
                                     is_logged_in,
                                     FALSE,
                                     (GdmChooserWidgetItemLoadFunc) on_item_load,
                                     widget);
        g_free (escaped_real_name);
        g_free (escaped_username);
        g_free (tooltip);

        if (pixbuf != NULL) {
                g_object_unref (pixbuf);
        }

        queue_update_other_user_visibility (widget);
}

static void
on_user_added (ActUserManager       *manager,
               ActUser              *user,
               GdmUserChooserWidget *widget)
{
        /* wait for all users to be loaded */
        if (! widget->priv->loaded) {
                return;
        }
        add_user (widget, user);
}

static void
on_user_removed (ActUserManager       *manager,
                 ActUser              *user,
                 GdmUserChooserWidget *widget)
{
        const char *user_name;

        g_debug ("GdmUserChooserWidget: User removed: %s", act_user_get_user_name (user));
        /* wait for all users to be loaded */
        if (! widget->priv->loaded) {
                return;
        }

        user_name = act_user_get_user_name (user);

        /* Ignore removals we aren't interested in */
        if (!gdm_chooser_widget_lookup_item (GDM_CHOOSER_WIDGET (widget),
                                             user_name,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL)) {
                return;
        }

        gdm_chooser_widget_remove_item (GDM_CHOOSER_WIDGET (widget),
                                        user_name);

        queue_update_other_user_visibility (widget);
}

static void
on_user_is_logged_in_changed (ActUserManager       *manager,
                              ActUser              *user,
                              GdmUserChooserWidget *widget)
{
        const char *user_name;
        gboolean    is_logged_in;

        g_debug ("GdmUserChooserWidget: User logged in changed: %s", act_user_get_user_name (user));

        user_name = act_user_get_user_name (user);
        is_logged_in = act_user_is_logged_in (user);

        gdm_chooser_widget_set_item_in_use (GDM_CHOOSER_WIDGET (widget),
                                            user_name,
                                            is_logged_in);
}

static void
on_user_changed (ActUserManager       *manager,
                 ActUser              *user,
                 GdmUserChooserWidget *widget)
{
        /* wait for all users to be loaded */
        if (! widget->priv->loaded) {
                return;
        }
        if (! widget->priv->show_normal_users) {
                return;
        }

        update_item_for_user (widget, user);
}

static gboolean
add_users (GdmUserChooserWidget *widget)
{
        guint cnt;

        cnt = 0;
        while (widget->priv->users_to_add != NULL && cnt < NUM_USERS_TO_ADD_PER_ITERATION) {
                add_user (widget, widget->priv->users_to_add->data);
                g_object_unref (widget->priv->users_to_add->data);
                widget->priv->users_to_add = g_slist_delete_link (widget->priv->users_to_add, widget->priv->users_to_add);
                cnt++;
        }
        g_debug ("GdmUserChooserWidget: added %u items", cnt);

        if (! widget->priv->loaded) {
                widget->priv->loaded = TRUE;

                gdm_chooser_widget_loaded (GDM_CHOOSER_WIDGET (widget));
        }

        if (widget->priv->users_to_add == NULL) {
            widget->priv->add_users_idle_id = 0;
            return FALSE;
        }

        return TRUE;
}

static void
queue_add_users (GdmUserChooserWidget *widget)
{
        if (widget->priv->add_users_idle_id == 0) {
                widget->priv->add_users_idle_id = g_idle_add ((GSourceFunc) add_users, widget);
        }
}

static void
on_is_loaded_changed (ActUserManager       *manager,
                      GParamSpec           *pspec,
                      GdmUserChooserWidget *widget)
{
        GSList *users;

        /* FIXME: handle is-loaded=FALSE */

        g_debug ("GdmUserChooserWidget: Users loaded");

        users = act_user_manager_list_users (manager);
        g_slist_foreach (users, (GFunc) g_object_ref, NULL);
        widget->priv->users_to_add = g_slist_concat (widget->priv->users_to_add, g_slist_copy (users));

        queue_add_users (widget);
}

static gboolean
load_users (GdmUserChooserWidget *widget)
{

        if (widget->priv->show_normal_users) {
                widget->priv->manager = act_user_manager_get_default ();

                g_signal_connect (widget->priv->manager,
                                  "user-added",
                                  G_CALLBACK (on_user_added),
                                  widget);
                g_signal_connect (widget->priv->manager,
                                  "user-removed",
                                  G_CALLBACK (on_user_removed),
                                  widget);
                g_signal_connect (widget->priv->manager,
                                  "notify::is-loaded",
                                  G_CALLBACK (on_is_loaded_changed),
                                  widget);
                g_signal_connect (widget->priv->manager,
                                  "user-is-logged-in-changed",
                                  G_CALLBACK (on_user_is_logged_in_changed),
                                  widget);
                g_signal_connect (widget->priv->manager,
                                  "user-changed",
                                  G_CALLBACK (on_user_changed),
                                  widget);
        } else {
                gdm_chooser_widget_loaded (GDM_CHOOSER_WIDGET (widget));
        }

        widget->priv->load_idle_id = 0;

        return FALSE;
}

static GObject *
gdm_user_chooser_widget_constructor (GType                  type,
                                     guint                  n_construct_properties,
                                     GObjectConstructParam *construct_properties)
{
        GdmUserChooserWidget      *widget;

        widget = GDM_USER_CHOOSER_WIDGET (G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->constructor (type,
                                                                                                              n_construct_properties,
                                                                                                              construct_properties));

        widget->priv->show_normal_users = !is_user_list_disabled (widget);

        widget->priv->load_idle_id = g_idle_add ((GSourceFunc)load_users, widget);

        return G_OBJECT (widget);
}

static void
gdm_user_chooser_widget_dispose (GObject *object)
{
        GdmUserChooserWidget *widget;

        widget = GDM_USER_CHOOSER_WIDGET (object);

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->dispose (object);

        if (widget->priv->load_idle_id > 0) {
                g_source_remove (widget->priv->load_idle_id);
                widget->priv->load_idle_id = 0;
        }

        if (widget->priv->add_users_idle_id > 0) {
                g_source_remove (widget->priv->add_users_idle_id);
                widget->priv->add_users_idle_id = 0;
        }

        if (widget->priv->update_other_visibility_idle_id > 0) {
                g_source_remove (widget->priv->update_other_visibility_idle_id);
                widget->priv->update_other_visibility_idle_id = 0;
        }

        if (widget->priv->users_to_add != NULL) {
                g_slist_foreach (widget->priv->users_to_add, (GFunc) g_object_ref, NULL);
                g_slist_free (widget->priv->users_to_add);
                widget->priv->users_to_add = NULL;
        }

        if (widget->priv->logged_in_pixbuf != NULL) {
                g_object_unref (widget->priv->logged_in_pixbuf);
                widget->priv->logged_in_pixbuf = NULL;
        }

        if (widget->priv->stock_person_pixbuf != NULL) {
                g_object_unref (widget->priv->stock_person_pixbuf);
                widget->priv->stock_person_pixbuf = NULL;
        }

        if (widget->priv->manager != NULL) {
                g_signal_handlers_disconnect_by_func (widget->priv->manager,
                                                      G_CALLBACK (on_user_added),
                                                      widget);
                g_signal_handlers_disconnect_by_func (widget->priv->manager,
                                                      G_CALLBACK (on_user_removed),
                                                      widget);
                g_signal_handlers_disconnect_by_func (widget->priv->manager,
                                                      G_CALLBACK (on_is_loaded_changed),
                                                      widget);
                g_signal_handlers_disconnect_by_func (widget->priv->manager,
                                                      G_CALLBACK (on_user_is_logged_in_changed),
                                                      widget);
                g_signal_handlers_disconnect_by_func (widget->priv->manager,
                                                      G_CALLBACK (on_user_changed),
                                                      widget);

                g_object_unref (widget->priv->manager);
                widget->priv->manager = NULL;
        }
}

static void
gdm_user_chooser_widget_class_init (GdmUserChooserWidgetClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_user_chooser_widget_get_property;
        object_class->set_property = gdm_user_chooser_widget_set_property;
        object_class->constructor = gdm_user_chooser_widget_constructor;
        object_class->dispose = gdm_user_chooser_widget_dispose;
        object_class->finalize = gdm_user_chooser_widget_finalize;


        g_object_class_install_property (object_class,
                                         PROP_SHOW_USER_AUTO,
                                         g_param_spec_boolean ("show-user-auto",
                                                               "show user auto",
                                                               "show user auto",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SHOW_USER_GUEST,
                                         g_param_spec_boolean ("show-user-guest",
                                                               "show user guest",
                                                               "show user guest",
                                                               FALSE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
        g_object_class_install_property (object_class,
                                         PROP_SHOW_USER_OTHER,
                                         g_param_spec_boolean ("show-user-other",
                                                               "show user other",
                                                               "show user other",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_type_class_add_private (klass, sizeof (GdmUserChooserWidgetPrivate));
}

static GdkPixbuf *
get_pixbuf_from_icon_names (GdmUserChooserWidget *widget,
                            const char           *first_name,
                            ...)
{
        GdkPixbuf   *pixbuf;
        GtkIconInfo *icon_info;
        GPtrArray   *array;
        int          size;
        const char  *icon_name;
        va_list      argument_list;
        gint        *sizes;
        gint         candidate_size;
        int          i;

        array = g_ptr_array_new ();

        g_ptr_array_add (array, (gpointer) first_name);

        va_start (argument_list, first_name);
        icon_name = (const char *) va_arg (argument_list, const char *);
        while (icon_name != NULL) {
                g_ptr_array_add (array, (gpointer) icon_name);
                icon_name = (const char *) va_arg (argument_list, const char *);
        }
        va_end (argument_list);
        g_ptr_array_add (array, NULL);

        size = get_icon_height_for_widget (GTK_WIDGET (widget));

        sizes = gtk_icon_theme_get_icon_sizes (widget->priv->icon_theme, first_name);

        candidate_size = 0;
        for (i = 0; sizes[i] != 0; i++) {

                /* scalable */
                if (sizes[i] == -1) {
                        candidate_size = sizes[i];
                        break;
                }

                if (ABS (size - sizes[i]) < ABS (size - candidate_size)) {
                        candidate_size = sizes[i];
                }
        }

        if (candidate_size == 0) {
                candidate_size = size;
        }

        icon_info = gtk_icon_theme_choose_icon (widget->priv->icon_theme,
                                                (const char **) array->pdata,
                                                candidate_size,
                                                GTK_ICON_LOOKUP_GENERIC_FALLBACK);
        g_ptr_array_free (array, FALSE);

        if (icon_info != NULL) {
                GError *error;

                error = NULL;
                pixbuf = gtk_icon_info_load_icon (icon_info, &error);
                gtk_icon_info_free (icon_info);

                if (error != NULL) {
                        g_warning ("Could not load icon '%s': %s",
                                   first_name, error->message);
                        g_error_free (error);
                }
        } else {
                GdkPixbuf *scaled_pixbuf;

                guchar pixel = 0x00000000;

                g_warning ("Could not find icon '%s' or fallbacks", first_name);
                pixbuf = gdk_pixbuf_new_from_data (&pixel, GDK_COLORSPACE_RGB,
                                                   TRUE, 8, 1, 1, 1, NULL, NULL);
                scaled_pixbuf = gdk_pixbuf_scale_simple (pixbuf, size, size, GDK_INTERP_NEAREST);
                g_object_unref (pixbuf);
                pixbuf = scaled_pixbuf;
        }

        return pixbuf;
}

static GdkPixbuf *
get_stock_person_pixbuf (GdmUserChooserWidget *widget)
{
        GdkPixbuf   *pixbuf;

        pixbuf = get_pixbuf_from_icon_names (widget,
                                             DEFAULT_USER_ICON,
                                             OLD_DEFAULT_USER_ICON,
                                             NULL);

        return pixbuf;
}

static GdkPixbuf *
get_logged_in_pixbuf (GdmUserChooserWidget *widget)
{
        GdkPixbuf *pixbuf;

        pixbuf = get_pixbuf_from_icon_names (widget,
                                             DEFAULT_USER_ICON,
                                             "emblem-default",
                                             NULL);

        return pixbuf;
}

typedef struct {
        GdkPixbuf *old_icon;
        GdkPixbuf *new_icon;
} IconUpdateData;

static gboolean
update_icons (GdmChooserWidget *widget,
              const char       *id,
              GdkPixbuf       **image,
              char            **name,
              char            **comment,
              gulong           *priority,
              gboolean         *is_in_use,
              gboolean         *is_separate,
              IconUpdateData   *data)
{
        if (data->old_icon == *image) {
                if (*image != NULL) {
                        g_object_unref (*image);
                }
                *image = data->new_icon;

                if (*image != NULL) {
                        g_object_ref (*image);
                }
                return TRUE;
        }

        return FALSE;
}

static void
load_icons (GdmUserChooserWidget *widget)
{
        GdkPixbuf     *old_pixbuf;
        IconUpdateData data;

        if (widget->priv->logged_in_pixbuf != NULL) {
                g_object_unref (widget->priv->logged_in_pixbuf);
        }
        widget->priv->logged_in_pixbuf = get_logged_in_pixbuf (widget);

        old_pixbuf = widget->priv->stock_person_pixbuf;
        widget->priv->stock_person_pixbuf = get_stock_person_pixbuf (widget);
        /* update the icons in the model */
        data.old_icon = old_pixbuf;
        data.new_icon = widget->priv->stock_person_pixbuf;
        gdm_chooser_widget_update_foreach_item (GDM_CHOOSER_WIDGET (widget),
                                                (GdmChooserUpdateForeachFunc)update_icons,
                                                &data);
        if (old_pixbuf != NULL) {
                g_object_unref (old_pixbuf);
        }
}

static void
on_icon_theme_changed (GtkIconTheme         *icon_theme,
                       GdmUserChooserWidget *widget)
{
        g_debug ("GdmUserChooserWidget: icon theme changed");
        load_icons (widget);
}

static void
setup_icons (GdmUserChooserWidget *widget)
{
        if (gtk_widget_has_screen (GTK_WIDGET (widget))) {
                widget->priv->icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (GTK_WIDGET (widget)));
        } else {
                widget->priv->icon_theme = gtk_icon_theme_get_default ();
        }

        if (widget->priv->icon_theme != NULL) {
                g_signal_connect (widget->priv->icon_theme,
                                  "changed",
                                  G_CALLBACK (on_icon_theme_changed),
                                  widget);
        }

        load_icons (widget);
}

static void
on_list_visible_changed (GdmChooserWidget *widget,
                         GParamSpec       *pspec,
                         gpointer          data)
{
        gboolean is_visible;

        g_object_get (G_OBJECT (widget), "list-visible", &is_visible, NULL);
        if (is_visible) {
                gtk_widget_grab_focus (GTK_WIDGET (widget));
        }
}

static void
gdm_user_chooser_widget_init (GdmUserChooserWidget *widget)
{
        widget->priv = GDM_USER_CHOOSER_WIDGET_GET_PRIVATE (widget);

        gdm_chooser_widget_set_separator_position (GDM_CHOOSER_WIDGET (widget),
                                                   GDM_CHOOSER_WIDGET_POSITION_BOTTOM);
        gdm_chooser_widget_set_in_use_message (GDM_CHOOSER_WIDGET (widget),
                                               _("Currently logged in"));

        g_signal_connect (widget,
                          "notify::list-visible",
                          G_CALLBACK (on_list_visible_changed),
                          NULL);

        setup_icons (widget);
}

static void
gdm_user_chooser_widget_finalize (GObject *object)
{
        GdmUserChooserWidget *widget;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_USER_CHOOSER_WIDGET (object));

        widget = GDM_USER_CHOOSER_WIDGET (object);

        g_return_if_fail (widget->priv != NULL);

        G_OBJECT_CLASS (gdm_user_chooser_widget_parent_class)->finalize (object);
}

GtkWidget *
gdm_user_chooser_widget_new (void)
{
        GObject *object;

        object = g_object_new (GDM_TYPE_USER_CHOOSER_WIDGET,
                               NULL);

        return GTK_WIDGET (object);
}
