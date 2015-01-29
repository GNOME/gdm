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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */
#include <X11/Xatom.h> /* for XA_PIXMAP */
#include <X11/cursorfont.h> /* for watch cursor */
#include <X11/Xatom.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#endif

#include "gdm-common.h"
#include "gdm-xerrors.h"

#include "gdm-slave.h"
#include "gdm-display.h"
#include "gdm-display-glue.h"

#include "gdm-server.h"

#define GDM_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SLAVE, GdmSlavePrivate))

struct GdmSlavePrivate
{
        GPid             pid;
        guint            output_watch_id;
        guint            error_watch_id;

        Display         *server_display;

        char            *session_id;

        GdmDisplay      *display;

        /* cached display values */
        char            *display_name;
        int              display_number;
        char            *display_hostname;
        gboolean         display_is_local;
        char            *display_seat_id;
        char            *display_x11_authority_file;
        char            *windowpath;
        GBytes          *display_x11_cookie;
        gboolean         display_is_initial;
};

enum {
        PROP_0,
        PROP_SESSION_ID,
        PROP_DISPLAY,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_NUMBER,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_X11_AUTHORITY_FILE,
        PROP_DISPLAY_IS_INITIAL,
};

enum {
        STARTED,
        STOPPED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_slave_class_init    (GdmSlaveClass *klass);
static void     gdm_slave_init          (GdmSlave      *slave);
static void     gdm_slave_finalize      (GObject       *object);

G_DEFINE_ABSTRACT_TYPE (GdmSlave, gdm_slave, G_TYPE_OBJECT)

#define CURSOR_WATCH XC_watch

GQuark
gdm_slave_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("gdm-slave-error-quark");
        }

        return ret;
}

static void
gdm_slave_setup_xhost_auth (XHostAddress *host_entries, XServerInterpretedAddress *si_entries)
{
        si_entries[0].type        = "localuser";
        si_entries[0].typelength  = strlen ("localuser");
        si_entries[1].type        = "localuser";
        si_entries[1].typelength  = strlen ("localuser");
        si_entries[2].type        = "localuser";
        si_entries[2].typelength  = strlen ("localuser");

        si_entries[0].value       = "root";
        si_entries[0].valuelength = strlen ("root");
        si_entries[1].value       = GDM_USERNAME;
        si_entries[1].valuelength = strlen (GDM_USERNAME);
        si_entries[2].value       = "gnome-initial-setup";
        si_entries[2].valuelength = strlen ("gnome-initial-setup");

        host_entries[0].family    = FamilyServerInterpreted;
        host_entries[0].address   = (char *) &si_entries[0];
        host_entries[0].length    = sizeof (XServerInterpretedAddress);
        host_entries[1].family    = FamilyServerInterpreted;
        host_entries[1].address   = (char *) &si_entries[1];
        host_entries[1].length    = sizeof (XServerInterpretedAddress);
        host_entries[2].family    = FamilyServerInterpreted;
        host_entries[2].address   = (char *) &si_entries[2];
        host_entries[2].length    = sizeof (XServerInterpretedAddress);
}

static void
gdm_slave_set_windowpath (GdmSlave *slave)
{
        /* setting WINDOWPATH for clients */
        Atom prop;
        Atom actualtype;
        int actualformat;
        unsigned long nitems;
        unsigned long bytes_after;
        unsigned char *buf;
        const char *windowpath;
        char *newwindowpath;
        unsigned long num;
        char nums[10];
        int numn;

        prop = XInternAtom (slave->priv->server_display, "XFree86_VT", False);
        if (prop == None) {
                g_debug ("no XFree86_VT atom\n");
                return;
        }
        if (XGetWindowProperty (slave->priv->server_display,
                DefaultRootWindow (slave->priv->server_display), prop, 0, 1,
                False, AnyPropertyType, &actualtype, &actualformat,
                &nitems, &bytes_after, &buf)) {
                g_debug ("no XFree86_VT property\n");
                return;
        }

        if (nitems != 1) {
                g_debug ("%lu items in XFree86_VT property!\n", nitems);
                XFree (buf);
                return;
        }

        switch (actualtype) {
        case XA_CARDINAL:
        case XA_INTEGER:
        case XA_WINDOW:
                switch (actualformat) {
                case  8:
                        num = (*(uint8_t  *)(void *)buf);
                        break;
                case 16:
                        num = (*(uint16_t *)(void *)buf);
                        break;
                case 32:
                        num = (*(long *)(void *)buf);
                        break;
                default:
                        g_debug ("format %d in XFree86_VT property!\n", actualformat);
                        XFree (buf);
                        return;
                }
                break;
        default:
                g_debug ("type %lx in XFree86_VT property!\n", actualtype);
                XFree (buf);
                return;
        }
        XFree (buf);

        windowpath = getenv ("WINDOWPATH");
        numn = snprintf (nums, sizeof (nums), "%lu", num);
        if (!windowpath) {
                newwindowpath = malloc (numn + 1);
                sprintf (newwindowpath, "%s", nums);
        } else {
                newwindowpath = malloc (strlen (windowpath) + 1 + numn + 1);
                sprintf (newwindowpath, "%s:%s", windowpath, nums);
        }

        slave->priv->windowpath = newwindowpath;

        g_setenv ("WINDOWPATH", newwindowpath, TRUE);
}

gboolean
gdm_slave_connect_to_x11_display (GdmSlave *slave)
{
        gboolean ret;

        ret = FALSE;

        /* We keep our own (windowless) connection (dsp) open to avoid the
         * X server resetting due to lack of active connections. */

        g_debug ("GdmSlave: Server is ready - opening display %s", slave->priv->display_name);

        /* Give slave access to the display independent of current hostname */
        if (slave->priv->display_x11_cookie != NULL) {
                XSetAuthorization ("MIT-MAGIC-COOKIE-1",
                                   strlen ("MIT-MAGIC-COOKIE-1"),
                                   (gpointer)
                                   g_bytes_get_data (slave->priv->display_x11_cookie, NULL),
                                   g_bytes_get_size (slave->priv->display_x11_cookie));
        }

        slave->priv->server_display = XOpenDisplay (slave->priv->display_name);

        if (slave->priv->server_display == NULL) {
                g_warning ("Unable to connect to display %s", slave->priv->display_name);
                ret = FALSE;
        } else if (slave->priv->display_is_local) {
                XServerInterpretedAddress si_entries[3];
                XHostAddress              host_entries[3];
                int                       i;

                g_debug ("GdmSlave: Connected to display %s", slave->priv->display_name);
                ret = TRUE;

                /* Give programs run by the slave and greeter access to the
                 * display independent of current hostname
                 */
                gdm_slave_setup_xhost_auth (host_entries, si_entries);

                gdm_error_trap_push ();

                for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                        XAddHost (slave->priv->server_display, &host_entries[i]);
                }

                XSync (slave->priv->server_display, False);
                if (gdm_error_trap_pop ()) {
                        g_warning ("Failed to give slave programs access to the display. Trying to proceed.");
                }

                gdm_slave_set_windowpath (slave);
        } else {
                g_debug ("GdmSlave: Connected to display %s", slave->priv->display_name);
                ret = TRUE;
        }

        if (ret) {
                g_signal_emit (slave, signals [STARTED], 0);
        }

        return ret;
}

static gboolean
gdm_slave_real_start (GdmSlave *slave)
{
        gboolean    res;
        GError     *error;
        const char *x11_cookie;
        gsize       x11_cookie_size;

        g_debug ("GdmSlave: Starting slave");

        /* cache some values up front */
        error = NULL;
        res = gdm_display_is_local (slave->priv->display, &slave->priv->display_is_local, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_get_x11_display_name (slave->priv->display, &slave->priv->display_name, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_get_x11_display_number (slave->priv->display, &slave->priv->display_number, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_get_remote_hostname (slave->priv->display, &slave->priv->display_hostname, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_get_x11_cookie (slave->priv->display, &x11_cookie, &x11_cookie_size, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        if (x11_cookie != NULL && x11_cookie_size > 0) {
                g_bytes_unref (slave->priv->display_x11_cookie);
                slave->priv->display_x11_cookie = g_bytes_new (x11_cookie, x11_cookie_size);
        }

        error = NULL;
        res = gdm_display_get_x11_authority_file (slave->priv->display, &slave->priv->display_x11_authority_file, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_get_seat_id (slave->priv->display, &slave->priv->display_seat_id, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_display_is_initial (slave->priv->display, &slave->priv->display_is_initial, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
}

static gboolean
gdm_slave_real_stop (GdmSlave *slave)
{
        g_debug ("GdmSlave: Stopping slave");

        g_clear_object (&slave->priv->display);

        return TRUE;
}

gboolean
gdm_slave_start (GdmSlave *slave)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        g_debug ("GdmSlave: starting slave");

        g_object_ref (slave);
        ret = GDM_SLAVE_GET_CLASS (slave)->start (slave);
        g_object_unref (slave);

        return ret;
}

gboolean
gdm_slave_stop (GdmSlave *slave)
{
        gboolean ret;

        g_return_val_if_fail (GDM_IS_SLAVE (slave), FALSE);

        g_debug ("GdmSlave: stopping slave");

        g_object_ref (slave);

        ret = GDM_SLAVE_GET_CLASS (slave)->stop (slave);
        g_signal_emit (slave, signals [STOPPED], 0);

        g_object_unref (slave);
        return ret;
}

gboolean
gdm_slave_add_user_authorization (GdmSlave   *slave,
                                  const char *username,
                                  char      **filenamep)
{
        XServerInterpretedAddress si_entries[3];
        XHostAddress              host_entries[3];
        int                       i;
        gboolean                  res;
        GError                   *error;
        char                     *filename;

        filename = NULL;

        if (filenamep != NULL) {
                *filenamep = NULL;
        }

        g_debug ("GdmSlave: Requesting user authorization");

        error = NULL;
        res = gdm_display_add_user_authorization (slave->priv->display,
                                                  username,
                                                  &filename,
                                                  &error);

        if (! res) {
                g_warning ("Failed to add user authorization: %s", error->message);
                g_error_free (error);
        } else {
                g_debug ("GdmSlave: Got user authorization: %s", filename);
        }

        if (filenamep != NULL) {
                *filenamep = g_strdup (filename);
        }
        g_free (filename);

        /* Remove access for the programs run by slave and greeter now that the
         * user session is starting.
         */
        gdm_slave_setup_xhost_auth (host_entries, si_entries);
        gdm_error_trap_push ();
        for (i = 0; i < G_N_ELEMENTS (host_entries); i++) {
                XRemoveHost (slave->priv->server_display, &host_entries[i]);
        }
        XSync (slave->priv->server_display, False);
        if (gdm_error_trap_pop ()) {
                g_warning ("Failed to remove slave program access to the display. Trying to proceed.");
        }

        return res;
}

static char *
gdm_slave_parse_enriched_login (GdmSlave   *slave,
                                const char *username)
{
        char     **argv;
        int        username_len;
        GPtrArray *env;
        GError    *error;
        gboolean   res;
        char      *parsed_username;
        char      *command;
        char      *std_output;
        char      *std_error;

        parsed_username = NULL;

        if (username == NULL || username[0] == '\0') {
                return NULL;
        }

        /* A script may be used to generate the automatic/timed login name
           based on the display/host by ending the name with the pipe symbol
           '|'. */

        username_len = strlen (username);
        if (username[username_len - 1] != '|') {
                return g_strdup (username);
        }

        /* Remove the pipe symbol */
        command = g_strndup (username, username_len - 1);

        argv = NULL;
        error = NULL;
        if (! g_shell_parse_argv (command, NULL, &argv, &error)) {
                g_warning ("GdmSlave: Could not parse command '%s': %s", command, error->message);
                g_error_free (error);

                g_free (command);
                goto out;
        }

        g_debug ("GdmSlave: running '%s' to acquire auto/timed username", command);
        g_free (command);

        env = gdm_get_script_environment (username,
                                          slave->priv->display_name,
                                          slave->priv->display_hostname,
                                          slave->priv->display_x11_authority_file);

        error = NULL;
        std_output = NULL;
        std_error = NULL;
        res = g_spawn_sync (NULL,
                            argv,
                            (char **)env->pdata,
                            G_SPAWN_SEARCH_PATH,
                            NULL,
                            NULL,
                            &std_output,
                            &std_error,
                            NULL,
                            &error);

        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);
        g_strfreev (argv);

        if (! res) {
                g_warning ("GdmSlave: Unable to launch auto/timed login script '%s': %s", username, error->message);
                g_error_free (error);

                g_free (std_output);
                g_free (std_error);
                goto out;
        }

        if (std_output != NULL) {
                g_strchomp (std_output);
                if (std_output[0] != '\0') {
                        parsed_username = g_strdup (std_output);
                }
        }

 out:
        return parsed_username;
}

gboolean
gdm_slave_get_timed_login_details (GdmSlave   *slave,
                                   gboolean   *enabledp,
                                   char      **usernamep,
                                   int        *delayp)
{
        struct passwd *pwent;
        gboolean       enabled;
        char          *username;
        int            delay;

        username = NULL;
        enabled = FALSE;
        delay = 0;

        g_debug ("GdmSlave: Requesting timed login details");

        gdm_display_get_timed_login_details (slave->priv->display,
                                             &enabled,
                                             &username,
                                             &delay);

        g_debug ("GdmSlave: Got timed login details: %d %s %d", enabled, username, delay);

        if (usernamep != NULL) {
                *usernamep = gdm_slave_parse_enriched_login (slave, username);
        } else {
                g_free (username);

                if (enabledp != NULL) {
                        *enabledp = enabled;
                }
                if (delayp != NULL) {
                        *delayp = delay;
                }
                return TRUE;
        }
        g_free (username);

        if (usernamep != NULL && *usernamep != NULL) {
                gdm_get_pwent_for_name (*usernamep, &pwent);
                if (pwent == NULL) {
                        g_debug ("Invalid username %s for auto/timed login",
                                 *usernamep);
                        g_free (*usernamep);
                        *usernamep = NULL;
                } else {
                        g_debug ("Using username %s for auto/timed login",
                                 *usernamep);

                        if (enabledp != NULL) {
                                *enabledp = enabled;
                        }
                        if (delayp != NULL) {
                                *delayp = delay;
                        }
               }
        } else {
                g_debug ("Invalid NULL username for auto/timed login");
        }

        return TRUE;
}

static void
_gdm_slave_set_session_id (GdmSlave   *slave,
                           const char *id)
{
        g_free (slave->priv->session_id);
        slave->priv->session_id = g_strdup (id);
}

static void
gdm_slave_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
        GdmSlave *self;

        self = GDM_SLAVE (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                _gdm_slave_set_session_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY:
                self->priv->display = g_value_dup_object (value);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_slave_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
        GdmSlave *self;

        self = GDM_SLAVE (object);

        switch (prop_id) {
        case PROP_SESSION_ID:
                g_value_set_string (value, self->priv->session_id);
                break;
        case PROP_DISPLAY:
                g_value_set_object (value, self->priv->display);
                break;
        case PROP_DISPLAY_NAME:
                g_value_set_string (value, self->priv->display_name);
                break;
        case PROP_DISPLAY_NUMBER:
                g_value_set_int (value, self->priv->display_number);
                break;
        case PROP_DISPLAY_HOSTNAME:
                g_value_set_string (value, self->priv->display_hostname);
                break;
        case PROP_DISPLAY_SEAT_ID:
                g_value_set_string (value, self->priv->display_seat_id);
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                g_value_set_string (value, self->priv->display_x11_authority_file);
                break;
        case PROP_DISPLAY_IS_LOCAL:
                g_value_set_boolean (value, self->priv->display_is_local);
                break;
        case PROP_DISPLAY_IS_INITIAL:
                g_value_set_boolean (value, self->priv->display_is_initial);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
gdm_slave_class_init (GdmSlaveClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_slave_get_property;
        object_class->set_property = gdm_slave_set_property;
        object_class->finalize = gdm_slave_finalize;

        klass->start = gdm_slave_real_start;
        klass->stop = gdm_slave_real_stop;

        g_type_class_add_private (klass, sizeof (GdmSlavePrivate));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_ID,
                                         g_param_spec_string ("session-id",
                                                              "Session id",
                                                              "ID of session",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY,
                                         g_param_spec_object ("display",
                                                              "id",
                                                              "id",
                                                              GDM_TYPE_DISPLAY,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NUMBER,
                                         g_param_spec_int ("display-number",
                                                           "display number",
                                                           "display number",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "display hostname",
                                                              "display hostname",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("display-x11-authority-file",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READABLE));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_INITIAL,
                                         g_param_spec_boolean ("display-is-initial",
                                                               NULL,
                                                               NULL,
                                                               FALSE,
                                                               G_PARAM_READABLE));

        signals [STARTED] =
                g_signal_new ("started",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);

        signals [STOPPED] =
                g_signal_new ("stopped",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (GdmSlaveClass, stopped),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
}

static void
gdm_slave_init (GdmSlave *slave)
{
        slave->priv = GDM_SLAVE_GET_PRIVATE (slave);

        slave->priv->pid = -1;
}

static void
gdm_slave_finalize (GObject *object)
{
        GdmSlave *slave;

        g_return_if_fail (object != NULL);
        g_return_if_fail (GDM_IS_SLAVE (object));

        slave = GDM_SLAVE (object);

        g_return_if_fail (slave->priv != NULL);

        g_free (slave->priv->display_name);
        g_free (slave->priv->display_hostname);
        g_free (slave->priv->display_seat_id);
        g_free (slave->priv->display_x11_authority_file);
        g_bytes_unref (slave->priv->display_x11_cookie);

        G_OBJECT_CLASS (gdm_slave_parent_class)->finalize (object);
}
