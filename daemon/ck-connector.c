/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * ck-connector.c : Code for login managers to register with ConsoleKit.
 *
 * Copyright (c) 2007 David Zeuthen <davidz@redhat.com>
 * Copyright (c) 2007 William Jon McCann <mccann@jhu.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <gio/gio.h>
#include <gobject/gvaluecollector.h>

#include "ck-connector.h"

struct _CkConnector
{
        int              refcount;
        char            *cookie;
        gboolean         session_created;
        GDBusConnection *connection;
};

static struct {
        const char *name;
        const char *variant_type;
        GType       gtype;
} parameter_lookup[] = {
        { "display-device",     "s", G_TYPE_STRING },
        { "x11-display-device", "s", G_TYPE_STRING },
        { "x11-display",        "s", G_TYPE_STRING },
        { "remote-host-name",   "s", G_TYPE_STRING },
        { "session-type",       "s", G_TYPE_STRING },
        { "is-local",           "b", G_TYPE_BOOLEAN },
        { "unix-user",          "i", G_TYPE_INT },
};

static void
lookup_parameter_type (const char *name, const char **variant_type, GType *gtype)
{
        int i;

        *gtype = G_TYPE_INVALID;

        for (i = 0; i < G_N_ELEMENTS (parameter_lookup); i++) {
                if (strcmp (name, parameter_lookup[i].name) == 0) {
                        *variant_type = parameter_lookup[i].variant_type;
                        *gtype = parameter_lookup[i].gtype;
                        break;
                }
        }
}

/* Frees all resources allocated and disconnects from the system
 * message bus.
 */
static void
_ck_connector_free (CkConnector *connector)
{
        g_clear_object (&connector->connection);

        if (connector->cookie != NULL) {
                g_free (connector->cookie);
        }

        g_slice_free (CkConnector, connector);
}

/**
 * Decrements the reference count of a CkConnector, disconnecting
 * from the bus and freeing the connector if the count reaches 0.
 *
 * @param connector the connector
 * @see ck_connector_ref
 */
void
ck_connector_unref (CkConnector *connector)
{
        g_return_if_fail (connector != NULL);

        /* Probably should use some kind of atomic op here */
        connector->refcount -= 1;
        if (connector->refcount == 0) {
                _ck_connector_free (connector);
        }
}

/**
 * Increments the reference count of a CkConnector.
 *
 * @param connector the connector
 * @returns the connector
 * @see ck_connector_unref
 */
CkConnector *
ck_connector_ref (CkConnector *connector)
{
        g_return_val_if_fail (connector != NULL, NULL);

        /* Probably should use some kind of atomic op here */
        connector->refcount += 1;

        return connector;
}

/**
 * Constructs a new Connector to communicate with the ConsoleKit
 * daemon. Returns #NULL if memory can't be allocated for the
 * object.
 *
 * @returns a new CkConnector, free with ck_connector_unref()
 */
CkConnector *
ck_connector_new (void)
{
        CkConnector *connector;

        connector = g_slice_new (CkConnector);

        connector->refcount = 1;
        connector->connection = NULL;
        connector->cookie = NULL;
        connector->session_created = FALSE;

        return connector;
}

static gboolean
open_session_helper (CkConnector   *connector,
                     const char    *method,
                     GVariant      *parameters,
                     GError       **error)
{
        GError    *local_error = NULL;
        gboolean   ret;
        GVariant  *result;

        g_return_val_if_fail (connector != NULL, FALSE);
        g_return_val_if_fail (error == NULL || !*error, FALSE);

        ret = FALSE;

        connector->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &local_error);
        if (connector->connection == NULL) {
                g_propagate_prefixed_error (error, local_error, "Unable to open session: ");
                goto out;
        }

        result = g_dbus_connection_call_sync (connector->connection,
                                              "org.freedesktop.ConsoleKit",
                                              "/org/freedesktop/ConsoleKit/Manager",
                                              "org.freedesktop.ConsoleKit.Manager",
                                              "OpenSession",
                                              NULL,
                                              (const GVariantType*) "(s)",
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                              -1,
                                              NULL,
                                              &local_error);

        if (result == NULL) {
                g_propagate_prefixed_error (error, local_error, "Unable to open session: ");
                goto out;
        }

        g_variant_get (result, "(s)", &connector->cookie);
        g_variant_unref (result);

        connector->session_created = TRUE;
        ret = TRUE;

out:
        return ret;
}

/**
 * Connects to the D-Bus system bus daemon and issues the method call
 * OpenSession on the ConsoleKit manager interface.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 *
 * @returns #TRUE if the operation succeeds
 */
gboolean
ck_connector_open_session (CkConnector  *connector,
                           GError      **error)
{
        return open_session_helper (connector,
                                    "OpenSession",
                                    NULL,
                                    error);
}

static GVariant *
make_parameters (const char *first_parameter_name,
                 va_list     var_args)
{
        GVariantBuilder builder;
        const char *p = first_parameter_name;

        g_variant_builder_init (&builder, (const GVariantType*) "a{sv}");

        while (p) {
                const char *variant_type;
                GType gtype;
                GValue value = G_VALUE_INIT;
                GVariant *element;
                char **error = NULL;

                lookup_parameter_type (p, &variant_type, &gtype);
                g_assert (gtype != G_TYPE_INVALID);

                G_VALUE_COLLECT_INIT (&value, gtype, var_args, 0, error);

                if (error)
                        g_free (*error);

                element = g_dbus_gvalue_to_gvariant (&value, (const GVariantType*) variant_type);
                g_value_unset (&value);

                g_variant_builder_add (&builder, p, element);

                p = va_arg (var_args, char*);
        }

        return g_variant_builder_end (&builder);
}

static gboolean
ck_connector_open_session_with_parameters_valist (CkConnector  *connector,
                                                  GError      **error,
                                                  const char   *first_parameter_name,
                                                  va_list       var_args)
{
        return open_session_helper (connector,
                                    "OpenSessionWithParameters",
                                    make_parameters (first_parameter_name, var_args),
                                    error);
}

/**
 * Opens a new session with parameter from variable argument list. The
 * variable argument list should contain the name of each parameter
 * followed by the value to append.
 * For example:
 *
 * @code
 *
 * DBusError    error;
 * dbus_int32_t v_INT32 = 500;
 * const char  *v_STRING = "/dev/tty3";
 *
 * dbus_error_init (&error);
 * ck_connector_open_session_with_parameters (connector,
 *                                            &error,
 *                                            "unix-user", &v_INT32,
 *                                            "display-device", &v_STRING,
 *                                            NULL);
 * @endcode
 *
 * @param error error output
 * @param first_parameter_name name of the first parameter
 * @param ... value of first parameter, list of additional name-value pairs
 * @returns #TRUE on success
 */
gboolean
ck_connector_open_session_with_parameters (CkConnector  *connector,
                                           GError      **error,
                                           const char   *first_parameter_name,
                                           ...)
{
        va_list     var_args;
        gboolean ret;

        g_return_val_if_fail (connector != NULL, FALSE);
        g_return_val_if_fail (error == NULL || !*error, FALSE);

        va_start (var_args, first_parameter_name);
        ret = ck_connector_open_session_with_parameters_valist (connector,
                                                                error,
                                                                first_parameter_name,
                                                                var_args);
        va_end (var_args);

        return ret;
}

/**
 * Connects to the D-Bus system bus daemon and issues the method call
 * OpenSessionWithParameters on the ConsoleKit manager interface. The
 * connection to the bus is private.
 *
 * The only parameter that is optional is x11_display - it may be set
 * to NULL if there is no X11 server associated with the session.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 *
 * @param user UID for the user owning the session
 * @param display_device the tty device for the session
 * @param x11_display the value of the X11 DISPLAY for the session
 * @returns #TRUE if the operation succeeds
 */
gboolean
ck_connector_open_session_for_user (CkConnector  *connector,
                                    uid_t         user,
                                    const char   *display_device,
                                    const char   *x11_display,
                                    GError      **error)
{
        g_return_val_if_fail (connector != NULL, FALSE);
        g_return_val_if_fail (display_device != NULL, FALSE);

        return ck_connector_open_session_with_parameters (connector,
                                                          error,
                                                          "display-device", display_device,
                                                          "x11-display", x11_display,
                                                          "unix-user", user,
                                                          NULL);
}

/**
 * Gets the cookie for the current open session.
 * Returns #NULL if no session is open.
 *
 * @returns a constant string with the cookie.
 */
const char *
ck_connector_get_cookie (CkConnector *connector)
{
        g_return_val_if_fail (connector != NULL, NULL);

        if (! connector->session_created) {
                return NULL;
        } else {
                return connector->cookie;
        }
}

/**
 * Issues the CloseSession method call on the ConsoleKit manager
 * interface.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running, if the caller doesn't have
 * sufficient privilege or if a session isn't open.
 *
 * @returns #TRUE if the operation succeeds
 */
gboolean
ck_connector_close_session (CkConnector  *connector,
                            GError      **error)
{
        GError   *local_error = NULL;
        gboolean  ret, closed;
        GVariant *result;

        g_return_val_if_fail (connector != NULL, FALSE);
        g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

        ret = FALSE;

        if (!connector->session_created || connector->cookie == NULL) {
                g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to close session: no session open");
                goto out;
        }

        result = g_dbus_connection_call_sync (connector->connection,
                                              "org.freedesktop.ConsoleKit",
                                              "/org/freedesktop/ConsoleKit/Manager",
                                              "org.freedesktop.ConsoleKit.Manager",
                                              "CloseSession",
                                              g_variant_new ("(s)", connector->cookie),
                                              (const GVariantType*) "(b)",
                                              G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                              -1,
                                              NULL,
                                              &local_error);

        if (result == NULL) {
                g_propagate_prefixed_error (error, local_error, "Unable to close session: ");
                goto out;
        }

        g_variant_get (result, "(b)", &closed);
        g_variant_unref (result);

        if (! closed) {
                goto out;
        }

        connector->session_created = FALSE;
        ret = TRUE;

out:
        return ret;
}
