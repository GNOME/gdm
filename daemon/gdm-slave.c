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
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include <X11/Xlib.h> /* for Display */
#include <X11/Xatom.h> /* for XA_PIXMAP */
#include <X11/cursorfont.h> /* for watch cursor */
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

#ifdef HAVE_LIBXKLAVIER
#include <libxklavier/xklavier.h>
#endif

#ifdef WITH_SYSTEMD
#include <systemd/sd-login.h>
#include <systemd/sd-daemon.h>
#endif

#include "gdm-common.h"
#include "gdm-xerrors.h"

#include "gdm-slave.h"
#include "gdm-slave-glue.h"
#include "gdm-display-glue.h"

#include "gdm-server.h"

#define GDM_SLAVE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GDM_TYPE_SLAVE, GdmSlavePrivate))

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

#define GDM_DBUS_NAME              "org.gnome.DisplayManager"
#define GDM_DBUS_DISPLAY_INTERFACE "org.gnome.DisplayManager.Display"

#define MAX_CONNECT_ATTEMPTS 10

struct GdmSlavePrivate
{
        char            *id;
        GPid             pid;
        guint            output_watch_id;
        guint            error_watch_id;

        Display         *server_display;

        /* cached display values */
        char            *display_id;
        char            *display_name;
        int              display_number;
        char            *display_hostname;
        gboolean         display_is_local;
        gboolean         display_is_parented;
        char            *display_seat_id;
        char            *display_x11_authority_file;
        char            *parent_display_name;
        char            *parent_display_x11_authority_file;
        char            *windowpath;
        char            *display_x11_cookie;

        GdmDBusDisplay  *display_proxy;
        GDBusConnection *connection;
        GDBusObjectSkeleton *object_skeleton;
        GdmDBusSlave    *skeleton;
};

enum {
        PROP_0,
        PROP_DISPLAY_ID,
        PROP_DISPLAY_NAME,
        PROP_DISPLAY_NUMBER,
        PROP_DISPLAY_HOSTNAME,
        PROP_DISPLAY_IS_LOCAL,
        PROP_DISPLAY_SEAT_ID,
        PROP_DISPLAY_X11_AUTHORITY_FILE
};

enum {
        STOPPED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     gdm_slave_class_init    (GdmSlaveClass *klass);
static void     gdm_slave_init          (GdmSlave      *slave);
static void     gdm_slave_finalize      (GObject       *object);

G_DEFINE_ABSTRACT_TYPE (GdmSlave, gdm_slave, G_TYPE_OBJECT)

#define CURSOR_WATCH XC_watch

static void
gdm_slave_whack_temp_auth_file (GdmSlave *slave)
{
#if 0
        uid_t old;

        old = geteuid ();
        if (old != 0)
                seteuid (0);
        if (d->parent_temp_auth_file != NULL) {
                VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
        }
        g_free (d->parent_temp_auth_file);
        d->parent_temp_auth_file = NULL;
        if (old != 0)
                seteuid (old);
#endif
}


static void
create_temp_auth_file (GdmSlave *slave)
{
#if 0
        if (d->type == TYPE_FLEXI_XNEST &&
            d->parent_auth_file != NULL) {
                if (d->parent_temp_auth_file != NULL) {
                        VE_IGNORE_EINTR (g_unlink (d->parent_temp_auth_file));
                }
                g_free (d->parent_temp_auth_file);
                d->parent_temp_auth_file =
                        copy_auth_file (d->server_uid,
                                        gdm_daemon_config_get_gdmuid (),
                                        d->parent_auth_file);
        }
#endif
}

static void
listify_hash (const char *key,
              const char *value,
              GPtrArray  *env)
{
        char *str;
        str = g_strdup_printf ("%s=%s", key, value);
        g_debug ("GdmSlave: script environment: %s", str);
        g_ptr_array_add (env, str);
}

static GPtrArray *
get_script_environment (GdmSlave   *slave,
                        const char *username)
{
        GPtrArray     *env;
        GHashTable    *hash;
        struct passwd *pwent;
        char          *x_servers_file;
        char          *temp;

        env = g_ptr_array_new ();

        /* create a hash table of current environment, then update keys has necessary */
        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

        /* modify environment here */
        g_hash_table_insert (hash, g_strdup ("HOME"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("PWD"), g_strdup ("/"));
        g_hash_table_insert (hash, g_strdup ("SHELL"), g_strdup ("/bin/sh"));

        if (username != NULL) {
                g_hash_table_insert (hash, g_strdup ("LOGNAME"),
                                     g_strdup (username));
                g_hash_table_insert (hash, g_strdup ("USER"),
                                     g_strdup (username));
                g_hash_table_insert (hash, g_strdup ("USERNAME"),
                                     g_strdup (username));

                gdm_get_pwent_for_name (username, &pwent);
                if (pwent != NULL) {
                        if (pwent->pw_dir != NULL && pwent->pw_dir[0] != '\0') {
                                g_hash_table_insert (hash, g_strdup ("HOME"),
                                                     g_strdup (pwent->pw_dir));
                                g_hash_table_insert (hash, g_strdup ("PWD"),
                                                     g_strdup (pwent->pw_dir));
                        }

                        g_hash_table_insert (hash, g_strdup ("SHELL"),
                                             g_strdup (pwent->pw_shell));
                }
        }

#if 0
        if (display_is_parented) {
                g_hash_table_insert (hash, g_strdup ("GDM_PARENT_DISPLAY"), g_strdup (parent_display_name));

                /*g_hash_table_insert (hash, "GDM_PARENT_XAUTHORITY"), slave->priv->parent_temp_auth_file));*/
        }
#endif

        /* some env for use with the Pre and Post scripts */
        temp = g_strconcat (slave->priv->display_name, ".Xservers", NULL);
        x_servers_file = g_build_filename (AUTHDIR, temp, NULL);
        g_free (temp);

        g_hash_table_insert (hash, g_strdup ("X_SERVERS"), x_servers_file);

        if (! slave->priv->display_is_local) {
                g_hash_table_insert (hash, g_strdup ("REMOTE_HOST"), g_strdup (slave->priv->display_hostname));
        }

        /* Runs as root */
        g_hash_table_insert (hash, g_strdup ("XAUTHORITY"), g_strdup (slave->priv->display_x11_authority_file));
        g_hash_table_insert (hash, g_strdup ("DISPLAY"), g_strdup (slave->priv->display_name));
        g_hash_table_insert (hash, g_strdup ("PATH"), g_strdup (GDM_SESSION_DEFAULT_PATH));
        g_hash_table_insert (hash, g_strdup ("RUNNING_UNDER_GDM"), g_strdup ("true"));

        g_hash_table_remove (hash, "MAIL");


        g_hash_table_foreach (hash, (GHFunc)listify_hash, env);
        g_hash_table_destroy (hash);

        g_ptr_array_add (env, NULL);

        return env;
}

gboolean
gdm_slave_run_script (GdmSlave   *slave,
                      const char *dir,
                      const char *login)
{
        char      *script;
        char     **argv;
        gint       status;
        GError    *error;
        GPtrArray *env;
        gboolean   res;
        gboolean   ret;

        ret = FALSE;

        g_assert (dir != NULL);
        g_assert (login != NULL);

        script = g_build_filename (dir, slave->priv->display_name, NULL);
        g_debug ("GdmSlave: Trying script %s", script);
        if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
               && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                g_debug ("GdmSlave: script %s not found; skipping", script);
                g_free (script);
                script = NULL;
        }

        if (script == NULL
            && slave->priv->display_hostname != NULL
            && slave->priv->display_hostname[0] != '\0') {
                script = g_build_filename (dir, slave->priv->display_hostname, NULL);
                g_debug ("GdmSlave: Trying script %s", script);
                if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
                       && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                        g_debug ("GdmSlave: script %s not found; skipping", script);
                        g_free (script);
                        script = NULL;
                }
        }

        if (script == NULL) {
                script = g_build_filename (dir, "Default", NULL);
                g_debug ("GdmSlave: Trying script %s", script);
                if (! (g_file_test (script, G_FILE_TEST_IS_REGULAR)
                       && g_file_test (script, G_FILE_TEST_IS_EXECUTABLE))) {
                        g_debug ("GdmSlave: script %s not found; skipping", script);
                        g_free (script);
                        script = NULL;
                }
        }

        if (script == NULL) {
                g_debug ("GdmSlave: no script found");
                return TRUE;
        }

        create_temp_auth_file (slave);

        g_debug ("GdmSlave: Running process: %s", script);
        error = NULL;
        if (! g_shell_parse_argv (script, NULL, &argv, &error)) {
                g_warning ("Could not parse command: %s", error->message);
                g_error_free (error);
                goto out;
        }

        env = get_script_environment (slave, login);

        res = g_spawn_sync (NULL,
                            argv,
                            (char **)env->pdata,
                            G_SPAWN_SEARCH_PATH,
                            NULL,
                            NULL,
                            NULL,
                            NULL,
                            &status,
                            &error);

        g_ptr_array_foreach (env, (GFunc)g_free, NULL);
        g_ptr_array_free (env, TRUE);
        g_strfreev (argv);

        if (! res) {
                g_warning ("GdmSlave: Unable to run script: %s", error->message);
                g_error_free (error);
        }

        gdm_slave_whack_temp_auth_file (slave);

        if (WIFEXITED (status)) {
                g_debug ("GdmSlave: Process exit status: %d", WEXITSTATUS (status));
                ret = WEXITSTATUS (status) != 0;
        } else {
                ret = TRUE;
        }

 out:
        g_free (script);

        return ret;
}

static void
gdm_slave_save_root_window_of_screen (GdmSlave *slave,
                                      Atom      id_atom,
                                      int       screen_number)
{
        Window root_window;
        GC gc;
        XGCValues values;
        Pixmap pixmap;
        int width, height, depth;

        root_window = RootWindow (slave->priv->server_display,
                                  screen_number);

        width = DisplayWidth (slave->priv->server_display, screen_number);
        height = DisplayHeight (slave->priv->server_display, screen_number);
        depth = DefaultDepth (slave->priv->server_display, screen_number);
        pixmap = XCreatePixmap (slave->priv->server_display,
                                root_window,
                                width, height, depth);

        values.function = GXcopy;
        values.plane_mask = AllPlanes;
        values.fill_style = FillSolid;
        values.subwindow_mode = IncludeInferiors;

        gc = XCreateGC (slave->priv->server_display,
                        root_window,
                        GCFunction | GCPlaneMask | GCFillStyle | GCSubwindowMode,
                        &values);

        if (XCopyArea (slave->priv->server_display,
                       root_window, pixmap, gc, 0, 0,
                       width, height, 0, 0)) {

                long pixmap_as_long;

                pixmap_as_long = (long) pixmap;

                XChangeProperty (slave->priv->server_display,
                                 root_window, id_atom, XA_PIXMAP,
                                 32, PropModeReplace, (guchar *) &pixmap_as_long,
                                 1);

        }

        XFreeGC (slave->priv->server_display, gc);
}

void
gdm_slave_save_root_windows (GdmSlave *slave)
{
        int i, number_of_screens;
        Atom atom;

        number_of_screens = ScreenCount (slave->priv->server_display);

        atom = XInternAtom (slave->priv->server_display,
                            "_XROOTPMAP_ID", False);

        if (atom == 0) {
                return;
        }

        for (i = 0; i < number_of_screens; i++) {
                gdm_slave_save_root_window_of_screen (slave, atom, i);
        }

        XSync (slave->priv->server_display, False);
}

void
gdm_slave_set_initial_keyboard_layout (GdmSlave *slave)
{
#ifdef HAVE_LIBXKLAVIER
        XklEngine    *engine;
        XklConfigRec *config;

        engine = xkl_engine_get_instance (slave->priv->server_display);
        config = xkl_config_rec_new ();

        if (xkl_config_rec_get_from_server (config, engine)) {
                int i;

                for (i = 1; config->layouts[i] != NULL; i++) {
                        /* put us at the front of the list, since usernames and
                         * passwords are usually ascii
                         */
                        if (strcmp (config->layouts[i], "us") == 0) {
                                char *temp_layout;
                                char *temp_variant = NULL;
                                char *temp_options = NULL;

                                temp_layout = config->layouts[0];
                                config->layouts[0] = config->layouts[i];
                                config->layouts[i] = temp_layout;

                                if (config->variants != NULL) {
                                        temp_variant = config->variants[0];
                                        config->variants[0] = config->variants[i];
                                        config->variants[i] = temp_variant;
                                }

                                if (config->options != NULL) {
                                        temp_options = config->options[0];
                                        config->options[0] = config->options[i];
                                        config->options[i] = temp_options;
                                }
                                break;
                        }
                }
                xkl_config_rec_activate (config, engine);
        }
#endif
}

static void
determine_initial_cursor_position (GdmSlave *slave,
                                   int      *x,
                                   int      *y)
{
        XRRScreenResources *resources;
        RROutput primary_output;
        int i;

        /* If this function fails for whatever reason,
         * put the pointer in the upper left corner of the
         * first monitor
         */
        *x = 0;
        *y = 0;

        gdm_error_trap_push ();
        resources = XRRGetScreenResources (slave->priv->server_display,
                                           DefaultRootWindow (slave->priv->server_display));
        primary_output = XRRGetOutputPrimary (slave->priv->server_display,
                                              DefaultRootWindow (slave->priv->server_display));
        gdm_error_trap_pop ();

        if (resources == NULL) {
                return;
        }

        for (i = 0; i < resources->noutput; i++) {
                XRROutputInfo *output_info;

                if (primary_output == None) {
                        primary_output = resources->outputs[0];
                }

                if (resources->outputs[i] != primary_output) {
                        continue;
                }

                output_info = XRRGetOutputInfo (slave->priv->server_display,
                                                resources,
                                                resources->outputs[i]);

                if (output_info->connection != RR_Disconnected &&
                    output_info->crtc != 0) {
                        XRRCrtcInfo *crtc_info;

                        crtc_info = XRRGetCrtcInfo (slave->priv->server_display,
                                                    resources,
                                                    output_info->crtc);
                        /* position it sort of in the lower right
                         */
                        *x = crtc_info->x + .9 * crtc_info->width;
                        *y = crtc_info->y + .9 * crtc_info->height;
                        XRRFreeCrtcInfo (crtc_info);
                }

                XRRFreeOutputInfo (output_info);
                break;
        }

        XRRFreeScreenResources (resources);
}

void
gdm_slave_set_initial_cursor_position (GdmSlave *slave)
{
        if (slave->priv->server_display != NULL) {
                int x, y;

                determine_initial_cursor_position (slave, &x, &y);
                XWarpPointer(slave->priv->server_display,
                             None,
                             DefaultRootWindow (slave->priv->server_display),
                             0, 0,
                             0, 0,
                             x, y);
        }
}

void
gdm_slave_set_busy_cursor (GdmSlave *slave)
{
        if (slave->priv->server_display != NULL) {
                Cursor xcursor;

                xcursor = XCreateFontCursor (slave->priv->server_display, CURSOR_WATCH);
                XDefineCursor (slave->priv->server_display,
                               DefaultRootWindow (slave->priv->server_display),
                               xcursor);
                XFreeCursor (slave->priv->server_display, xcursor);
                XSync (slave->priv->server_display, False);
        }
}

static void
gdm_slave_setup_xhost_auth (XHostAddress *host_entries, XServerInterpretedAddress *si_entries)
{
        si_entries[0].type        = "localuser";
        si_entries[0].typelength  = strlen ("localuser");
        si_entries[1].type        = "localuser";
        si_entries[1].typelength  = strlen ("localuser");

        si_entries[0].value       = "root";
        si_entries[0].valuelength = strlen ("root");
        si_entries[1].value       = GDM_USERNAME;
        si_entries[1].valuelength = strlen (GDM_USERNAME);

        host_entries[0].family    = FamilyServerInterpreted;
        host_entries[0].address   = (char *) &si_entries[0];
        host_entries[0].length    = sizeof (XServerInterpretedAddress);
        host_entries[1].family    = FamilyServerInterpreted;
        host_entries[1].address   = (char *) &si_entries[1];
        host_entries[1].length    = sizeof (XServerInterpretedAddress);
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
        sigset_t mask;
        sigset_t omask;

        ret = FALSE;

        /* We keep our own (windowless) connection (dsp) open to avoid the
         * X server resetting due to lack of active connections. */

        g_debug ("GdmSlave: Server is ready - opening display %s", slave->priv->display_name);

        g_setenv ("DISPLAY", slave->priv->display_name, TRUE);
        g_setenv ("XAUTHORITY", slave->priv->display_x11_authority_file, TRUE);

        sigemptyset (&mask);
        sigaddset (&mask, SIGCHLD);
        sigprocmask (SIG_BLOCK, &mask, &omask);

        /* Give slave access to the display independent of current hostname */
        XSetAuthorization ("MIT-MAGIC-COOKIE-1",
                           strlen ("MIT-MAGIC-COOKIE-1"),
                           slave->priv->display_x11_cookie,
                           strlen (slave->priv->display_x11_cookie));

        slave->priv->server_display = XOpenDisplay (slave->priv->display_name);

        sigprocmask (SIG_SETMASK, &omask, NULL);


        if (slave->priv->server_display == NULL) {
                g_warning ("Unable to connect to display %s", slave->priv->display_name);
                ret = FALSE;
        } else if (slave->priv->display_is_local) {
                XServerInterpretedAddress si_entries[2];
                XHostAddress              host_entries[2];

                g_debug ("GdmSlave: Connected to display %s", slave->priv->display_name);
                ret = TRUE;

                /* Give programs run by the slave and greeter access to the
                 * display independent of current hostname
                 */
                gdm_slave_setup_xhost_auth (host_entries, si_entries);

                gdm_error_trap_push ();
                XAddHosts (slave->priv->server_display, host_entries,
                           G_N_ELEMENTS (host_entries));
                XSync (slave->priv->server_display, False);
                if (gdm_error_trap_pop ()) {
                        g_warning ("Failed to give slave programs access to the display. Trying to proceed.");
                }

                gdm_slave_set_windowpath (slave);
        } else {
                g_debug ("GdmSlave: Connected to display %s", slave->priv->display_name);
                ret = TRUE;
        }

        return ret;
}

static gboolean
gdm_slave_set_slave_bus_name (GdmSlave *slave)
{
        gboolean    res;
        GError     *error;
        const char *name;

        name = g_dbus_connection_get_unique_name (slave->priv->connection);

        error = NULL;
        res = gdm_dbus_display_call_set_slave_bus_name_sync (slave->priv->display_proxy,
                                                             name,
                                                             NULL, &error);
        if (! res) {
                g_warning ("Failed to set slave bus name on parent: %s", error->message);
                g_error_free (error);
        }

        return res;
}

static gboolean
gdm_slave_real_start (GdmSlave *slave)
{
        gboolean res;
        char    *id;
        GError  *error;

        g_debug ("GdmSlave: Starting slave");

        g_assert (slave->priv->display_proxy == NULL);

        g_debug ("GdmSlave: Creating proxy for %s", slave->priv->display_id);
        error = NULL;
        slave->priv->display_proxy = GDM_DBUS_DISPLAY (gdm_dbus_display_proxy_new_sync (slave->priv->connection,
                                                                                        G_DBUS_PROXY_FLAGS_NONE,
                                                                                        GDM_DBUS_NAME,
                                                                                        slave->priv->display_id,
                                                                                        NULL, &error));

        if (slave->priv->display_proxy == NULL) {
                g_warning ("Failed to create display proxy %s: %s", slave->priv->display_id, error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_id_sync (slave->priv->display_proxy,
                                                 &id,
                                                 NULL, &error);
        if (! res) {
                g_warning ("Failed to get display ID %s: %s", slave->priv->display_id, error->message);
                g_error_free (error);
                return FALSE;
        }

        g_debug ("GdmSlave: Got display ID: %s", id);

        if (strcmp (id, slave->priv->display_id) != 0) {
                g_critical ("Display ID doesn't match");
                exit (1);
        }

        gdm_slave_set_slave_bus_name (slave);

        /* cache some values up front */
        error = NULL;
        res = gdm_dbus_display_call_is_local_sync (slave->priv->display_proxy,
                                                   &slave->priv->display_is_local,
                                                   NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_x11_display_name_sync (slave->priv->display_proxy,
                                                               &slave->priv->display_name,
                                                               NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_x11_display_number_sync (slave->priv->display_proxy,
                                                                 &slave->priv->display_number,
                                                                 NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_remote_hostname_sync (slave->priv->display_proxy,
                                                              &slave->priv->display_hostname,
                                                              NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_x11_cookie_sync (slave->priv->display_proxy,
                                                         &slave->priv->display_x11_cookie,
                                                         NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_x11_authority_file_sync (slave->priv->display_proxy,
                                                                 &slave->priv->display_x11_authority_file,
                                                                 NULL, &error);
        if (! res) {
                g_warning ("Failed to get value: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = gdm_dbus_display_call_get_seat_id_sync (slave->priv->display_proxy,
                                                      &slave->priv->display_seat_id,
                                                      NULL, &error);
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

        if (slave->priv->display_proxy != NULL) {
                g_object_unref (slave->priv->display_proxy);
        }

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
        g_object_unref (slave);

        return ret;
}

void
gdm_slave_stopped (GdmSlave *slave)
{
        g_return_if_fail (GDM_IS_SLAVE (slave));

        g_signal_emit (slave, signals [STOPPED], 0);
}

gboolean
gdm_slave_add_user_authorization (GdmSlave   *slave,
                                  const char *username,
                                  char      **filenamep)
{
        XServerInterpretedAddress si_entries[2];
        XHostAddress              host_entries[2];
        gboolean                  res;
        GError                   *error;
        char                     *filename;

        filename = NULL;

        if (filenamep != NULL) {
                *filenamep = NULL;
        }

        g_debug ("GdmSlave: Requesting user authorization");

        error = NULL;
        res = gdm_dbus_display_call_add_user_authorization_sync (slave->priv->display_proxy,
                                                                 username,
                                                                 &filename,
                                                                 NULL, &error);

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
        XRemoveHosts (slave->priv->server_display, host_entries,
                      G_N_ELEMENTS (host_entries));
        XSync (slave->priv->server_display, False);
        if (gdm_error_trap_pop ()) {
                g_warning ("Failed to remove slave program access to the display. Trying to proceed.");
        }


        return res;
}

static char *
gdm_slave_parse_enriched_login (GdmSlave   *slave,
                                const char *username,
                                const char *display_name)
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

        env = get_script_environment (slave, NULL);

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
        GError        *error;
        gboolean       res;
        gboolean       enabled;
        char          *username;
        int            delay;

        username = NULL;
        enabled = FALSE;
        delay = 0;

        g_debug ("GdmSlave: Requesting timed login details");

        error = NULL;
        res = gdm_dbus_display_call_get_timed_login_details_sync (slave->priv->display_proxy,
                                                                  &enabled,
                                                                  &username,
                                                                  &delay,
                                                                  NULL, &error);
        if (! res) {
                g_warning ("Failed to get timed login details: %s", error->message);
                g_error_free (error);
        } else {
                g_debug ("GdmSlave: Got timed login details: %d %s %d", enabled, username, delay);
        }

        if (usernamep != NULL) {
                *usernamep = gdm_slave_parse_enriched_login (slave,
                                                             username,
                                                             slave->priv->display_name);
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

        return res;
}

static gboolean
_get_uid_and_gid_for_user (const char *username,
                           uid_t      *uid,
                           gid_t      *gid)
{
        struct passwd *passwd_entry;

        g_assert (username != NULL);

        errno = 0;
        gdm_get_pwent_for_name (username, &passwd_entry);

        if (passwd_entry == NULL) {
                return FALSE;
        }

        if (uid != NULL) {
                *uid = passwd_entry->pw_uid;
        }

        if (gid != NULL) {
                *gid = passwd_entry->pw_gid;
        }

        return TRUE;
}

#ifdef WITH_CONSOLE_KIT

static gboolean
x11_session_is_on_seat (GdmSlave        *slave,
                        const char      *session_id,
                        const char      *seat_id)
{
        GError          *error;
        GVariant        *reply;
        char            *sid;
        gboolean         ret;
        char            *x11_display_device;
        char            *x11_display;

        ret = FALSE;
        sid = NULL;
        x11_display = NULL;
        x11_display_device = NULL;

        if (seat_id == NULL || seat_id[0] == '\0' || session_id == NULL || session_id[0] == '\0') {
                return FALSE;
        }

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetSeatId",
                                             NULL, /* parameters */
                                             (const GVariantType*) "(o)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_debug ("Failed to identify the current seat: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_get (reply, "(o)", &sid);
        g_variant_unref (reply);

        if (sid == NULL || sid[0] == '\0' || strcmp (sid, seat_id) != 0) {
                g_debug ("GdmSlave: session not on current seat: %s", seat_id);
                goto out;
        }

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetX11Display",
                                             NULL, /* parameters */
                                             (const GVariantType*) "(s)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (! reply) {
                g_error_free (error);
                goto out;
        }

        g_variant_get (reply, "(s)", &x11_display);
        g_variant_unref (reply);

        /* don't try to switch to our own session */
        if (x11_display == NULL || x11_display[0] == '\0'
            || strcmp (slave->priv->display_name, x11_display) == 0) {
                g_free (x11_display);
                goto out;
        }
        g_free (x11_display);

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             session_id,
                                             CK_SESSION_INTERFACE,
                                             "GetX11DisplayDevice",
                                             NULL, /* parameters */
                                             (const GVariantType*) "(s)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (! reply) {
                g_error_free (error);
                goto out;
        }

        g_variant_get (reply, "(s)", &x11_display_device);
        g_variant_unref (reply);

        if (x11_display_device == NULL || x11_display_device[0] == '\0') {
                g_free (x11_display_device);
                goto out;
        }

        ret = TRUE;
 out:
        g_free (x11_display_device);
        g_free (x11_display);
        g_free (sid);

        return ret;
}

#endif

#ifdef WITH_SYSTEMD
static char*
gdm_slave_get_primary_session_id_for_user_from_systemd (GdmSlave   *slave,
                                                        const char *username)
{
        int     res, i;
        char  **sessions;
        uid_t   uid;
        char   *primary_ssid;
        gboolean got_primary_ssid;

        primary_ssid = NULL;
        got_primary_ssid = FALSE;

        res = sd_seat_can_multi_session (slave->priv->display_seat_id);
        if (res < 0) {
                g_warning ("GdmSlave: Failed to determine whether seat is multi-session capable: %s", strerror (-res));
                return NULL;
        } else if (res == 0) {
                g_debug ("GdmSlave: seat is unable to activate sessions");
                return NULL;
        }

        if (! _get_uid_and_gid_for_user (username, &uid, NULL)) {
                g_debug ("GdmSlave: unable to determine uid for user: %s", username);
                return NULL;
        }

        res = sd_seat_get_sessions (slave->priv->display_seat_id, &sessions, NULL, NULL);
        if (res < 0) {
                g_warning ("GdmSlave: Failed to get sessions on seat: %s", strerror (-res));
                return NULL;
        }

        if (sessions == NULL) {
                g_debug ("GdmSlave: seat has no active sessions");
                return NULL;
        }

        for (i = 0; sessions[i] != NULL; i++) {
                char *type;
                gboolean is_active;
                gboolean is_x11;
                uid_t other;

                res = sd_session_get_type (sessions[i], &type);

                if (res < 0) {
                        g_warning ("GdmSlave: could not fetch type of session '%s': %s",
                                   sessions[i], strerror (-res));
                        continue;
                }

                is_x11 = g_strcmp0 (type, "x11") == 0;
                g_free (type);

                /* Only migrate to graphical sessions
                 */
                if (!is_x11) {
                        continue;
                }

                /* Always give preference to non-active sessions,
                 * so we migrate when we can and don't when we can't
                 */
                is_active = sd_session_is_active (sessions[i]) > 0;

                res = sd_session_get_uid (sessions[i], &other);
                if (res == 0 && other == uid && !got_primary_ssid) {
                        g_free (primary_ssid);
                        primary_ssid = g_strdup (sessions[i]);

                        if (!is_active) {
                                got_primary_ssid = TRUE;
                        }
                }
                free (sessions[i]);
        }

        free (sessions);
        return primary_ssid;
}
#endif

#ifdef WITH_CONSOLE_KIT
static char *
gdm_slave_get_primary_session_id_for_user_from_ck (GdmSlave   *slave,
                                                   const char *username)
{
        gboolean    can_activate_sessions;
        GError     *error;
        char       *primary_ssid;
        uid_t       uid;
        GVariant   *reply;
        GVariantIter iter;
        char       *ssid;

        primary_ssid = NULL;

        g_debug ("GdmSlave: getting proxy for seat: %s", slave->priv->display_seat_id);
        g_debug ("GdmSlave: checking if seat can activate sessions");

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             slave->priv->display_seat_id,
                                             CK_SEAT_INTERFACE,
                                             "CanActivateSessions",
                                             NULL, /* parameters */
                                             (const GVariantType*) "(b)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_warning ("unable to determine if seat can activate sessions: %s", error->message);
                g_error_free (error);
                return NULL;
        }

        g_variant_get (reply, "(b)", &can_activate_sessions);
        g_variant_unref (reply);

        if (! can_activate_sessions) {
                g_debug ("GdmSlave: seat is unable to activate sessions");
                return NULL;
        }

        if (! _get_uid_and_gid_for_user (username, &uid, NULL)) {
                g_debug ("GdmSlave: unable to determine uid for user: %s", username);
                return NULL;
        }

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             CK_MANAGER_PATH,
                                             CK_MANAGER_INTERFACE,
                                             "CanSessionsForUnixUser",
                                             g_variant_new ("(u)", uid),
                                             (const GVariantType*) "(ao)",
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);

        if (! reply) {
                g_warning ("unable to determine sessions for user: %s", error->message);
                g_error_free (error);
                return NULL;
        }

        g_variant_iter_init (&iter, reply);
        while (g_variant_iter_loop (&iter, "(&s)", &ssid)) {
                if (x11_session_is_on_seat (slave, ssid, slave->priv->display_seat_id)) {
                        primary_ssid = g_strdup (ssid);
                        break;
                }
        }

        g_variant_unref (reply);
        return primary_ssid;
}
#endif

char *
gdm_slave_get_primary_session_id_for_user (GdmSlave   *slave,
                                           const char *username)
{

        if (slave->priv->display_seat_id == NULL || slave->priv->display_seat_id[0] == '\0') {
                g_debug ("GdmSlave: display seat ID is not set; can't switch sessions");
                return NULL;
        }

#ifdef WITH_SYSTEMD
        if (sd_booted () > 0) {
                return gdm_slave_get_primary_session_id_for_user_from_systemd (slave, username);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return gdm_slave_get_primary_session_id_for_user_from_ck (slave, username);
#else
        return NULL;
#endif
}

#ifdef WITH_SYSTEMD
static gboolean
activate_session_id_for_systemd (GdmSlave   *slave,
                                 const char *seat_id,
                                 const char *session_id)
{
        GError *error = NULL;
        GVariant *reply;

        /* Can't activate what's already active. We want this
         * to fail, because we don't want migration to succeed
         * if the only active session is the one just created
         * at the login screen.
         */
        if (sd_session_is_active (session_id) > 0) {
                return FALSE;
        }

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "ActivateSessionOnSeat",
                                             g_variant_new ("(ss)", session_id, seat_id),
                                             NULL, /* expected reply */
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_debug ("GdmSlave: logind %s raised:\n %s\n\n",
                         g_dbus_error_get_remote_error (error), error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}
#endif

#ifdef WITH_CONSOLE_KIT
static gboolean
activate_session_id_for_ck (GdmSlave   *slave,
                            const char *seat_id,
                            const char *session_id)
{
        GError *error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             seat_id,
                                             "org.freedesktop.ConsoleKit.Seat",
                                             "ActivateSession",
                                             g_variant_new ("(o)", session_id),
                                             NULL, /* expected reply */
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_debug ("GdmSlave: ConsoleKit %s raised:\n %s\n\n",
                         g_dbus_error_get_remote_error (error), error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}
#endif

static gboolean
activate_session_id (GdmSlave   *slave,
                     const char *seat_id,
                     const char *session_id)
{

#ifdef WITH_SYSTEMD
        if (sd_booted () > 0) {
                return activate_session_id_for_systemd (slave, seat_id, session_id);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return activate_session_id_for_ck (slave, seat_id, session_id);
#else
        return FALSE;
#endif
}

#ifdef WITH_SYSTEMD
static gboolean
session_unlock_for_systemd (GdmSlave   *slave,
                            const char *ssid)
{
        GError *error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             "org.freedesktop.login1",
                                             "/org/freedesktop/login1",
                                             "org.freedesktop.login1.Manager",
                                             "UnlockSession",
                                             g_variant_new ("(s)", ssid),
                                             NULL, /* expected reply */
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_debug ("GdmSlave: logind %s raised:\n %s\n\n",
                         g_dbus_error_get_remote_error (error), error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}
#endif

#ifdef WITH_CONSOLE_KIT
static gboolean
session_unlock_for_ck (GdmSlave   *slave,
                       const char *ssid)
{
        GError *error = NULL;
        GVariant *reply;

        reply = g_dbus_connection_call_sync (slave->priv->connection,
                                             CK_NAME,
                                             ssid,
                                             CK_SESSION_INTERFACE,
                                             "Unlock",
                                             NULL, /* parameters */
                                             NULL, /* expected reply */
                                             G_DBUS_CALL_FLAGS_NONE,
                                             -1,
                                             NULL, &error);
        if (reply == NULL) {
                g_debug ("GdmSlave: ConsoleKit %s raised:\n %s\n\n",
                         g_dbus_error_get_remote_error (error), error->message);
                g_error_free (error);
                return FALSE;
        }

        g_variant_unref (reply);

        return TRUE;
}
#endif

static gboolean
session_unlock (GdmSlave   *slave,
                const char *ssid)
{

        g_debug ("Unlocking session %s", ssid);

#ifdef WITH_SYSTEMD
        if (sd_booted () > 0) {
                return session_unlock_for_systemd (slave, ssid);
        }
#endif

#ifdef WITH_CONSOLE_KIT
        return session_unlock_for_ck (slave, ssid);
#else
        return TRUE;
#endif
}

gboolean
gdm_slave_switch_to_user_session (GdmSlave   *slave,
                                  const char *username)
{
        gboolean    res;
        gboolean    ret;
        char       *ssid_to_activate;

        ret = FALSE;

        ssid_to_activate = gdm_slave_get_primary_session_id_for_user (slave, username);
        if (ssid_to_activate == NULL) {
                g_debug ("GdmSlave: unable to determine session to activate");
                goto out;
        }

        g_debug ("GdmSlave: Activating session: '%s'", ssid_to_activate);

        res = activate_session_id (slave, slave->priv->display_seat_id, ssid_to_activate);
        if (! res) {
                g_debug ("GdmSlave: unable to activate session: %s", ssid_to_activate);
                goto out;
        }

        res = session_unlock (slave, ssid_to_activate);
        if (!res) {
                /* this isn't fatal */
                g_debug ("GdmSlave: unable to unlock session: %s", ssid_to_activate);
        }

        ret = TRUE;

 out:
        g_free (ssid_to_activate);

        return ret;
}

static void
_gdm_slave_set_display_id (GdmSlave   *slave,
                           const char *id)
{
        g_free (slave->priv->display_id);
        slave->priv->display_id = g_strdup (id);
}

static void
_gdm_slave_set_display_name (GdmSlave   *slave,
                             const char *name)
{
        g_free (slave->priv->display_name);
        slave->priv->display_name = g_strdup (name);
}

static void
_gdm_slave_set_display_number (GdmSlave   *slave,
                               int         number)
{
        slave->priv->display_number = number;
}

static void
_gdm_slave_set_display_hostname (GdmSlave   *slave,
                                 const char *name)
{
        g_free (slave->priv->display_hostname);
        slave->priv->display_hostname = g_strdup (name);
}

static void
_gdm_slave_set_display_x11_authority_file (GdmSlave   *slave,
                                           const char *name)
{
        g_free (slave->priv->display_x11_authority_file);
        slave->priv->display_x11_authority_file = g_strdup (name);
}

static void
_gdm_slave_set_display_seat_id (GdmSlave   *slave,
                                const char *id)
{
        g_free (slave->priv->display_seat_id);
        slave->priv->display_seat_id = g_strdup (id);
}

static void
_gdm_slave_set_display_is_local (GdmSlave   *slave,
                                 gboolean    is)
{
        slave->priv->display_is_local = is;
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
        case PROP_DISPLAY_ID:
                _gdm_slave_set_display_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_NAME:
                _gdm_slave_set_display_name (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_NUMBER:
                _gdm_slave_set_display_number (self, g_value_get_int (value));
                break;
        case PROP_DISPLAY_HOSTNAME:
                _gdm_slave_set_display_hostname (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_SEAT_ID:
                _gdm_slave_set_display_seat_id (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_X11_AUTHORITY_FILE:
                _gdm_slave_set_display_x11_authority_file (self, g_value_get_string (value));
                break;
        case PROP_DISPLAY_IS_LOCAL:
                _gdm_slave_set_display_is_local (self, g_value_get_boolean (value));
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
        case PROP_DISPLAY_ID:
                g_value_set_string (value, self->priv->display_id);
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
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static gboolean
register_slave (GdmSlave *slave)
{
        GError *error;

        error = NULL;
        slave->priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (slave->priv->connection == NULL) {
                g_critical ("error getting system bus: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        slave->priv->object_skeleton = g_dbus_object_skeleton_new (slave->priv->id);
        slave->priv->skeleton = GDM_DBUS_SLAVE (gdm_dbus_slave_skeleton_new ());

        g_dbus_object_skeleton_add_interface (slave->priv->object_skeleton,
                                              G_DBUS_INTERFACE_SKELETON (slave->priv->skeleton));

        return TRUE;
}

static GObject *
gdm_slave_constructor (GType                  type,
                       guint                  n_construct_properties,
                       GObjectConstructParam *construct_properties)
{
        GdmSlave      *slave;
        gboolean       res;
        const char    *id;

        slave = GDM_SLAVE (G_OBJECT_CLASS (gdm_slave_parent_class)->constructor (type,
                                                                                 n_construct_properties,
                                                                                 construct_properties));
        /* Always match the slave id with the master */

        id = NULL;
        if (g_str_has_prefix (slave->priv->display_id, "/org/gnome/DisplayManager/Displays/")) {
                id = slave->priv->display_id + strlen ("/org/gnome/DisplayManager/Displays/");
        }

        g_assert (id != NULL);

        slave->priv->id = g_strdup_printf ("/org/gnome/DisplayManager/Slaves/%s", id);
        g_debug ("GdmSlave: Registering %s", slave->priv->id);

        res = register_slave (slave);
        if (! res) {
                g_warning ("Unable to register slave with system bus");
        }

        return G_OBJECT (slave);
}

static void
gdm_slave_class_init (GdmSlaveClass *klass)
{
        GObjectClass    *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = gdm_slave_get_property;
        object_class->set_property = gdm_slave_set_property;
        object_class->constructor = gdm_slave_constructor;
        object_class->finalize = gdm_slave_finalize;

        klass->start = gdm_slave_real_start;
        klass->stop = gdm_slave_real_stop;

        g_type_class_add_private (klass, sizeof (GdmSlavePrivate));

        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_ID,
                                         g_param_spec_string ("display-id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NAME,
                                         g_param_spec_string ("display-name",
                                                              "display name",
                                                              "display name",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_NUMBER,
                                         g_param_spec_int ("display-number",
                                                           "display number",
                                                           "display number",
                                                           -1,
                                                           G_MAXINT,
                                                           -1,
                                                           G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_HOSTNAME,
                                         g_param_spec_string ("display-hostname",
                                                              "display hostname",
                                                              "display hostname",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_SEAT_ID,
                                         g_param_spec_string ("display-seat-id",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_X11_AUTHORITY_FILE,
                                         g_param_spec_string ("display-x11-authority-file",
                                                              "",
                                                              "",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_DISPLAY_IS_LOCAL,
                                         g_param_spec_boolean ("display-is-local",
                                                               "display is local",
                                                               "display is local",
                                                               TRUE,
                                                               G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

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

        gdm_slave_real_stop (slave);

        g_free (slave->priv->id);
        g_free (slave->priv->display_id);
        g_free (slave->priv->display_name);
        g_free (slave->priv->display_hostname);
        g_free (slave->priv->display_seat_id);
        g_free (slave->priv->display_x11_authority_file);
        g_free (slave->priv->parent_display_name);
        g_free (slave->priv->parent_display_x11_authority_file);
        g_free (slave->priv->windowpath);
        g_free (slave->priv->display_x11_cookie);

        G_OBJECT_CLASS (gdm_slave_parent_class)->finalize (object);
}
