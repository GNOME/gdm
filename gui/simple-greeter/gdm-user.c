/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2004-2005 James M. Cape <jcape@ignore-your.tv>.
 * Copyright (C) 2007-2008 William Jon McCann <mccann@jhu.edu>
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

#include <config.h>

#include <float.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "gdm-user-private.h"

#define GDM_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), GDM_TYPE_USER, GdmUserClass))
#define GDM_IS_USER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GDM_TYPE_USER))
#define GDM_USER_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object), GDM_TYPE_USER, GdmUserClass))

#define GLOBAL_FACEDIR    DATADIR "/faces"
#define MAX_FILE_SIZE     65536

enum {
        CHANGED,
        SESSIONS_CHANGED,
        LAST_SIGNAL
};

struct _GdmUser {
        GObject         parent;

        uid_t           uid;
        char           *user_name;
        char           *real_name;
        char           *home_dir;
        char           *shell;
        GList          *sessions;
        gulong          login_frequency;
};

typedef struct _GdmUserClass
{
        GObjectClass parent_class;
} GdmUserClass;

static void gdm_user_finalize     (GObject      *object);

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GdmUser, gdm_user, G_TYPE_OBJECT)

static int
session_compare (const char *a,
                 const char *b)
{
        if (a == NULL) {
                return 1;
        } else if (b == NULL) {
                return -1;
        }

        return strcmp (a, b);
}

void
_gdm_user_add_session (GdmUser    *user,
                       const char *ssid)
{
        GList *li;

        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_list_find_custom (user->sessions, ssid, (GCompareFunc)session_compare);
        if (li == NULL) {
                g_debug ("GdmUser: adding session %s", ssid);
                user->sessions = g_list_prepend (user->sessions, g_strdup (ssid));
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("GdmUser: session already present: %s", ssid);
        }
}

void
_gdm_user_remove_session (GdmUser    *user,
                          const char *ssid)
{
        GList *li;

        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (ssid != NULL);

        li = g_list_find_custom (user->sessions, ssid, (GCompareFunc)session_compare);
        if (li != NULL) {
                g_debug ("GdmUser: removing session %s", ssid);
                g_free (li->data);
                user->sessions = g_list_delete_link (user->sessions, li);
                g_signal_emit (user, signals[SESSIONS_CHANGED], 0);
        } else {
                g_debug ("GdmUser: session not found: %s", ssid);
        }
}

guint
gdm_user_get_num_sessions (GdmUser    *user)
{
        return g_list_length (user->sessions);
}

static void
gdm_user_class_init (GdmUserClass *class)
{
        GObjectClass *gobject_class;

        gobject_class = G_OBJECT_CLASS (class);

        gobject_class->finalize = gdm_user_finalize;

        signals [CHANGED] =
                g_signal_new ("changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
        signals [SESSIONS_CHANGED] =
                g_signal_new ("sessions-changed",
                              G_TYPE_FROM_CLASS (class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);
}

static void
gdm_user_init (GdmUser *user)
{
        user->user_name = NULL;
        user->real_name = NULL;
        user->sessions = NULL;
}

static void
gdm_user_finalize (GObject *object)
{
        GdmUser *user;

        user = GDM_USER (object);

        g_free (user->user_name);
        g_free (user->real_name);

        if (G_OBJECT_CLASS (gdm_user_parent_class)->finalize)
                (*G_OBJECT_CLASS (gdm_user_parent_class)->finalize) (object);
}

/**
 * _gdm_user_update_from_pwent:
 * @user: the user object to update.
 * @pwent: the user data to use.
 *
 * Updates the properties of @user using the data in @pwent.
 *
 * Since: 1.0
 **/
void
_gdm_user_update_from_pwent (GdmUser             *user,
                             const struct passwd *pwent)
{
        gchar *real_name = NULL;
        gboolean changed;

        g_return_if_fail (GDM_IS_USER (user));
        g_return_if_fail (pwent != NULL);

        changed = FALSE;

        /* Display Name */
        if (pwent->pw_gecos && pwent->pw_gecos[0] != '\0') {
                gchar *first_comma = NULL;
                gchar *valid_utf8_name = NULL;

                if (g_utf8_validate (pwent->pw_gecos, -1, NULL)) {
                        valid_utf8_name = pwent->pw_gecos;
                        first_comma = strchr (valid_utf8_name, ',');
                } else {
                        g_warning ("User %s has invalid UTF-8 in GECOS field. "
                                   "It would be a good thing to check /etc/passwd.",
                                   pwent->pw_name ? pwent->pw_name : "");
                }

                if (first_comma) {
                        real_name = g_strndup (valid_utf8_name,
                                               (first_comma - valid_utf8_name));
                } else if (valid_utf8_name) {
                        real_name = g_strdup (valid_utf8_name);
                } else {
                        real_name = NULL;
                }

                if (real_name && real_name[0] == '\0') {
                        g_free (real_name);
                        real_name = NULL;
                }
        } else {
                real_name = NULL;
        }

        if (g_strcmp0 (real_name, user->real_name) != 0) {
                g_free (user->real_name);
                user->real_name = real_name;
                changed = TRUE;
        } else {
                g_free (real_name);
        }

        /* UID */
        if (pwent->pw_uid != user->uid) {
                user->uid = pwent->pw_uid;
                changed = TRUE;
        }

        /* Username */
        if (g_strcmp0 (pwent->pw_name, user->user_name) != 0) {
                g_free (user->user_name);
                user->user_name = g_strdup (pwent->pw_name);
                changed = TRUE;
        }

        if (changed) {
                g_signal_emit (user, signals[CHANGED], 0);
        }
}

/**
 * _gdm_user_update_login_frequency:
 * @user: the user object to update
 *
 * Updates the login frequency of @user
 *
 * Since: 1.0
 **/
void
_gdm_user_update_login_frequency (GdmUser *user,
                                  guint64  login_frequency)
{
        g_return_if_fail (GDM_IS_USER (user));

        if (login_frequency == user->login_frequency) {
                return;
        }

        user->login_frequency = login_frequency;
        g_signal_emit (user, signals[CHANGED], 0);
}

/**
 * gdm_user_get_uid:
 * @user: the user object to examine.
 *
 * Retrieves the ID of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

gulong
gdm_user_get_uid (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), -1);

        return user->uid;
}

/**
 * gdm_user_get_real_name:
 * @user: the user object to examine.
 *
 * Retrieves the display name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/
const char *
gdm_user_get_real_name (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return (user->real_name ? user->real_name : user->user_name);
}

/**
 * gdm_user_get_user_name:
 * @user: the user object to examine.
 *
 * Retrieves the login name of @user.
 *
 * Returns: a pointer to an array of characters which must not be modified or
 *  freed, or %NULL.
 *
 * Since: 1.0
 **/

const char *
gdm_user_get_user_name (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), NULL);

        return user->user_name;
}

gulong
gdm_user_get_login_frequency (GdmUser *user)
{
        g_return_val_if_fail (GDM_IS_USER (user), 0);

        return user->login_frequency;
}

int
gdm_user_collate (GdmUser *user1,
                  GdmUser *user2)
{
        const char *str1;
        const char *str2;
        gulong      num1;
        gulong      num2;
        guint       len1;
        guint       len2;

        g_return_val_if_fail (GDM_IS_USER (user1), 0);
        g_return_val_if_fail (GDM_IS_USER (user2), 0);

        num1 = user1->login_frequency;
        num2 = user2->login_frequency;

        if (num1 > num2) {
                return -1;
        }

        if (num1 < num2) {
                return 1;
        }


        len1 = g_list_length (user1->sessions);
        len2 = g_list_length (user2->sessions);

        if (len1 > len2) {
                return -1;
        }

        if (len1 < len2) {
                return 1;
        }

        /* if login frequency is equal try names */
        if (user1->real_name != NULL) {
                str1 = user1->real_name;
        } else {
                str1 = user1->user_name;
        }

        if (user2->real_name != NULL) {
                str2 = user2->real_name;
        } else {
                str2 = user2->user_name;
        }

        if (str1 == NULL && str2 != NULL) {
                return -1;
        }

        if (str1 != NULL && str2 == NULL) {
                return 1;
        }

        if (str1 == NULL && str2 == NULL) {
                return 0;
        }

        return g_utf8_collate (str1, str2);
}

static gboolean
check_user_file (const char *filename)
{
        gssize      max_file_size = MAX_FILE_SIZE;
        struct stat fileinfo;

        /* Exists/Readable? */
        if (stat (filename, &fileinfo) < 0) {
                return FALSE;
        }

        /* Is a regular file */
        if (G_UNLIKELY (!S_ISREG (fileinfo.st_mode))) {
                return FALSE;
        }

        /* Size is kosher? */
        if (G_UNLIKELY (fileinfo.st_size > max_file_size)) {
                return FALSE;
        }

        return TRUE;
}

static GdkPixbuf *
render_icon_from_cache (GdmUser *user,
                        int      icon_size)
{
        GdkPixbuf  *retval;
        char       *path;
        gboolean    res;

        path = g_build_filename (GDM_CACHE_DIR, user->user_name, "face", NULL);
        res = check_user_file (path);
        if (res) {
                retval = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                g_debug ("Could not access face icon %s", path);
                retval = NULL;
        }
        g_free (path);

        return retval;
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

gboolean
gdm_user_is_logged_in (GdmUser *user)
{
        return user->sessions != NULL;
}

GdkPixbuf *
gdm_user_render_icon (GdmUser   *user,
                      gint       icon_size)
{
        GdkPixbuf    *pixbuf;
        GdkPixbuf    *framed;
        char         *path;
        char         *tmp;
        gboolean      res;

        g_return_val_if_fail (GDM_IS_USER (user), NULL);
        g_return_val_if_fail (icon_size > 12, NULL);

        path = NULL;

        pixbuf = render_icon_from_cache (user, icon_size);
        if (pixbuf != NULL) {
                goto out;
        }

        /* Try ${GlobalFaceDir}/${username} */
        path = g_build_filename (GLOBAL_FACEDIR, user->user_name, NULL);
        res = check_user_file (path);
        if (res) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                g_debug ("Could not access global face icon %s", path);
                pixbuf = NULL;
        }

        g_free (path);
        if (pixbuf != NULL) {
                goto out;
        }

        /* Finally, ${GlobalFaceDir}/${username}.png */
        tmp = g_strconcat (user->user_name, ".png", NULL);
        path = g_build_filename (GLOBAL_FACEDIR, tmp, NULL);
        g_free (tmp);
        res = check_user_file (path);
        if (res) {
                pixbuf = gdk_pixbuf_new_from_file_at_size (path,
                                                           icon_size,
                                                           icon_size,
                                                           NULL);
        } else {
                g_debug ("Could not access global face icon %s", path);
                pixbuf = NULL;
        }
        g_free (path);
 out:

        if (pixbuf != NULL) {
                framed = frame_pixbuf (pixbuf);
                if (framed != NULL) {
                        g_object_unref (pixbuf);
                        pixbuf = framed;
                }
        }

        return pixbuf;
}

const char *
gdm_user_get_primary_session_id (GdmUser *user)
{
        if (!gdm_user_is_logged_in (user)) {
                g_debug ("User %s is not logged in, so has no primary session",
                         gdm_user_get_user_name (user));
                return NULL;
        }

        /* FIXME: better way to choose? */
        return user->sessions->data;
}

