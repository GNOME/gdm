/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * GDM - The GNOME Display Manager
 * Copyright (C) 1998, 1999, 2000 Martin K. Petersen <mkp@mkp.net>
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

/* Code for cookie handling. This really needs to be modularized to
 * support other XAuth types and possibly DECnet... */

#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <errno.h>
#include <pwd.h>

#include <X11/Xauth.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "filecheck.h"
#include "auth.h"

#include "gdm-common.h"
#include "gdm-address.h"
#include "gdm-log.h"

gboolean
gdm_auth_add_entry (int            display_num,
                    GdmAddress    *address,
                    GString       *binary_cookie,
                    FILE          *af,
                    GSList       **authlist)
{
        Xauth *xa;
        char  *dispnum;

        xa = malloc (sizeof (Xauth));

        if (xa == NULL) {
                return FALSE;
        }

        if (address == NULL) {
                xa->family = FamilyWild;
                xa->address = NULL;
                xa->address_length = 0;
        } else {
                gboolean res;
                char    *hostname;

                xa->family = gdm_address_get_family_type (address);

                res = gdm_address_get_hostname (address, &hostname);
                if (! res) {
                        free (xa);
                        return FALSE;
                }

                g_debug ("Got hostname: %s", hostname);

                xa->address = hostname;
                xa->address_length = strlen (xa->address);
        }

        dispnum = g_strdup_printf ("%d", display_num);
        xa->number = strdup (dispnum);
        xa->number_length = strlen (dispnum);
        g_free (dispnum);

        xa->name = strdup ("MIT-MAGIC-COOKIE-1");
        xa->name_length = strlen ("MIT-MAGIC-COOKIE-1");
        xa->data = malloc (16);
        if (xa->data == NULL) {
                free (xa->number);
                free (xa->name);
                free (xa->address);
                free (xa);
                return FALSE;
        }

        memcpy (xa->data, binary_cookie->str, binary_cookie->len);
        xa->data_length = binary_cookie->len;

        g_debug ("Writing auth for address:%p %s:%d", address, xa->address, display_num);

        if (af != NULL) {
                errno = 0;
                if ( ! XauWriteAuth (af, xa)) {
                        free (xa->data);
                        free (xa->number);
                        free (xa->name);
                        free (xa->address);
                        free (xa);

                        if (errno != 0) {
                                g_warning (_("%s: Could not write new authorization entry: %s"),
                                           "add_auth_entry", g_strerror (errno));
                        } else {
                                g_warning (_("%s: Could not write new authorization entry.  "
                                             "Possibly out of diskspace"),
                                           "add_auth_entry");
                        }

                        return FALSE;
                }
        }

        if (authlist != NULL) {
                *authlist = g_slist_append (*authlist, xa);
        }

        return TRUE;
}

gboolean
gdm_auth_add_entry_for_display (int         display_num,
                                GdmAddress *address,
                                GString    *cookie,
                                FILE       *af,
                                GSList    **authlist)
{
        GString *binary_cookie;
        gboolean ret;

        binary_cookie = g_string_new (NULL);

        if (! gdm_string_hex_decode (cookie,
                                     0,
                                     NULL,
                                     binary_cookie,
                                     0)) {
                ret = FALSE;
                goto out;
        }

        ret = gdm_auth_add_entry (display_num,
                                  address,
                                  binary_cookie,
                                  af,
                                  authlist);

 out:
        g_string_free (binary_cookie, TRUE);
        return ret;
}

gboolean
gdm_auth_user_add (int         display_num,
                   GdmAddress *address,
                   const char *username,
                   const char *cookie,
                   char      **filenamep)
{
        int            fd;
        char          *filename;
        GError        *error;
        mode_t         old_mask;
        FILE          *af;
        gboolean       ret;
        struct passwd *pwent;
        GString       *cookie_str;

        g_debug ("Add user auth for address:%p num:%d user:%s", address, display_num, username);

        ret = FALSE;
        filename = NULL;
        af = NULL;
        fd = -1;

        old_mask = umask (077);

        filename = NULL;
        error = NULL;
        fd = g_file_open_tmp (".gdmXXXXXX", &filename, &error);

        umask (old_mask);

        if (fd == -1) {
                g_warning ("Unable to create temporary file: %s", error->message);
                g_error_free (error);
                goto out;
        }

        if (filenamep != NULL) {
                *filenamep = g_strdup (filename);
        }

        VE_IGNORE_EINTR (af = fdopen (fd, "w"));
        if (af == NULL) {
                g_warning ("Unable to open cookie file: %s", filename);
                goto out;
        }

        /* FIXME: clean old files? */

        cookie_str = g_string_new (cookie);

        /* FIXME: ?? */
        /*gdm_auth_add_entry_for_display (display_num, address, cookie_str, af, NULL);*/
        gdm_auth_add_entry_for_display (display_num, NULL, cookie_str, af, NULL);
        g_string_free (cookie_str, TRUE);

        pwent = getpwnam (username);
        if (pwent == NULL) {
                goto out;
        }

        fchown (fd, pwent->pw_uid, -1);

        ret = TRUE;
 out:
        g_free (filename);

        if (af != NULL) {
                fclose (af);
        }

        return ret;
}
