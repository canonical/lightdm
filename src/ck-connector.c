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
#include <dbus/dbus.h>

#include "ck-connector.h"

#define N_ELEMENTS(arr)             (sizeof (arr) / sizeof ((arr)[0]))

#if defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define _CK_FUNCTION_NAME __func__
#elif defined(__GNUC__) || defined(_MSC_VER)
#define _CK_FUNCTION_NAME __FUNCTION__
#else
#define _CK_FUNCTION_NAME "unknown function"
#endif

#define CK_CONNECTOR_ERROR "org.freedesktop.CkConnector.Error"

#define _CK_WARNING_FORMAT "arguments to %s() were incorrect, assertion \"%s\" failed in file %s line %d.\n"
#define _ck_return_if_fail(condition) do {                                         \
  if (!(condition)) {                                                              \
          fprintf (stderr, _CK_WARNING_FORMAT, _CK_FUNCTION_NAME, #condition, __FILE__, __LINE__); \
    return;                                                                        \
  } } while (0)

#define _ck_return_val_if_fail(condition, val) do {                                     \
  if (!(condition)) {                                                              \
          fprintf (stderr, _CK_WARNING_FORMAT, _CK_FUNCTION_NAME, #condition, __FILE__, __LINE__); \
    return val;                                                                        \
  } } while (0)

struct _CkConnector
{
        int             refcount;
        char           *cookie;
        dbus_bool_t     session_created;
        DBusConnection *connection;
};

static struct {
        char *name;
        int   type;
} parameter_lookup[] = {
        { "display-device",     DBUS_TYPE_STRING },
        { "x11-display-device", DBUS_TYPE_STRING },
        { "x11-display",        DBUS_TYPE_STRING },
        { "remote-host-name",   DBUS_TYPE_STRING },
        { "session-type",       DBUS_TYPE_STRING },
        { "is-local",           DBUS_TYPE_BOOLEAN },
        { "unix-user",          DBUS_TYPE_INT32 },
};

static int
lookup_parameter_type (const char *name)
{
        int i;
        int type;

        type = DBUS_TYPE_INVALID;

        for (i = 0; i < N_ELEMENTS (parameter_lookup); i++) {
                if (strcmp (name, parameter_lookup[i].name) == 0) {
                        type = parameter_lookup[i].type;
                        break;
                }
        }

        return type;
}

static dbus_bool_t
add_param_basic (DBusMessageIter *iter_array,
                 const char      *name,
                 int              type,
                 const void      *value)
{
        DBusMessageIter iter_struct;
        DBusMessageIter iter_variant;
        const char     *container_type;

        switch (type) {
        case DBUS_TYPE_STRING:
                container_type = DBUS_TYPE_STRING_AS_STRING;
                break;
        case DBUS_TYPE_BOOLEAN:
                container_type = DBUS_TYPE_BOOLEAN_AS_STRING;
                break;
        case DBUS_TYPE_INT32:
                container_type = DBUS_TYPE_INT32_AS_STRING;
                break;
        default:
                goto oom;
                break;
        }

        if (! dbus_message_iter_open_container (iter_array,
                                                DBUS_TYPE_STRUCT,
                                                NULL,
                                                &iter_struct)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_struct,
                                              DBUS_TYPE_STRING,
                                              &name)) {
                goto oom;
        }

        if (! dbus_message_iter_open_container (&iter_struct,
                                                DBUS_TYPE_VARIANT,
                                                container_type,
                                                &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_append_basic (&iter_variant,
                                              type,
                                              value)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (&iter_struct,
                                                 &iter_variant)) {
                goto oom;
        }

        if (! dbus_message_iter_close_container (iter_array,
                                                 &iter_struct)) {
                goto oom;
        }

        return TRUE;
oom:
        return FALSE;
}

/* Frees all resources allocated and disconnects from the system
 * message bus.
 */
static void
_ck_connector_free (CkConnector *connector)
{
        if (connector->connection != NULL) {
                /* it's a private connection so it's all good */
                dbus_connection_close (connector->connection);
        }

        if (connector->cookie != NULL) {
                free (connector->cookie);
        }

        free (connector);
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
        _ck_return_if_fail (connector != NULL);

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
        _ck_return_val_if_fail (connector != NULL, NULL);

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

        connector = calloc (1, sizeof (CkConnector));
        if (connector == NULL) {
                goto oom;
        }

        connector->refcount = 1;
        connector->connection = NULL;
        connector->cookie = NULL;
        connector->session_created = FALSE;
oom:
        return connector;
}

/**
 * Connects to the D-Bus system bus daemon and issues the method call
 * OpenSession on the ConsoleKit manager interface. The
 * connection to the bus is private.
 *
 * Returns FALSE on OOM, if the system bus daemon is not running, if
 * the ConsoleKit daemon is not running or if the caller doesn't have
 * sufficient privileges.
 *
 * @returns #TRUE if the operation succeeds
 */
dbus_bool_t
ck_connector_open_session (CkConnector *connector,
                           DBusError   *error)
{
        DBusError    local_error;
        DBusMessage *message;
        DBusMessage *reply;
        dbus_bool_t  ret;
        char        *cookie;

        _ck_return_val_if_fail (connector != NULL, FALSE);
        _ck_return_val_if_fail ((error) == NULL || !dbus_error_is_set ((error)), FALSE);

        reply = NULL;
        message = NULL;
        ret = FALSE;

        dbus_error_init (&local_error);
        connector->connection = dbus_bus_get_private (DBUS_BUS_SYSTEM, &local_error);
        if (connector->connection == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                }

                goto out;
        }

        dbus_connection_set_exit_on_disconnect (connector->connection, FALSE);

        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "OpenSession");
        if (message == NULL) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_error_init (&local_error);
        if (! dbus_message_get_args (reply,
                                     &local_error,
                                     DBUS_TYPE_STRING, &cookie,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        connector->cookie = strdup (cookie);
        if (connector->cookie == NULL) {
                goto out;
        }

        connector->session_created = TRUE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;
}

static dbus_bool_t
ck_connector_open_session_with_parameters_valist (CkConnector *connector,
                                                  DBusError   *error,
                                                  const char  *first_parameter_name,
                                                  va_list      var_args)
{
        DBusError       local_error;
        DBusMessage    *message;
        DBusMessage    *reply;
        DBusMessageIter iter;
        DBusMessageIter iter_array;
        dbus_bool_t     ret;
        char           *cookie;
        const char     *name;

        _ck_return_val_if_fail (connector != NULL, FALSE);

        reply = NULL;
        message = NULL;
        ret = FALSE;

        dbus_error_init (&local_error);
        connector->connection = dbus_bus_get_private (DBUS_BUS_SYSTEM, &local_error);
        if (connector->connection == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                }
                goto out;
        }

        dbus_connection_set_exit_on_disconnect (connector->connection, FALSE);

        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "OpenSessionWithParameters");
        if (message == NULL) {
                goto out;
        }

        dbus_message_iter_init_append (message, &iter);
        if (! dbus_message_iter_open_container (&iter,
                                                DBUS_TYPE_ARRAY,
                                                "(sv)",
                                                &iter_array)) {
                goto out;
        }

        name = first_parameter_name;
        while (name != NULL) {
                int         type;
                const void *value;
                dbus_bool_t res;

                type = lookup_parameter_type (name);
                value = va_arg (var_args, const void *);

                if (type == DBUS_TYPE_INVALID) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unknown parameter: %s",
                                        name);
                        goto out;
                }

                res = add_param_basic (&iter_array, name, type, value);
                if (! res) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Error adding parameter: %s",
                                        name);
                        goto out;
                }

                name = va_arg (var_args, char *);
        }

        if (! dbus_message_iter_close_container (&iter, &iter_array)) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_error_init (&local_error);
        if (! dbus_message_get_args (reply,
                                     &local_error,
                                     DBUS_TYPE_STRING, &cookie,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to open session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        connector->cookie = strdup (cookie);
        if (connector->cookie == NULL) {
                goto out;
        }

        connector->session_created = TRUE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;
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
dbus_bool_t
ck_connector_open_session_with_parameters (CkConnector *connector,
                                           DBusError   *error,
                                           const char  *first_parameter_name,
                                           ...)
{
        va_list     var_args;
        dbus_bool_t ret;

        _ck_return_val_if_fail (connector != NULL, FALSE);
        _ck_return_val_if_fail ((error) == NULL || !dbus_error_is_set ((error)), FALSE);

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
dbus_bool_t
ck_connector_open_session_for_user (CkConnector *connector,
                                    uid_t        user,
                                    const char  *display_device,
                                    const char  *x11_display,
                                    DBusError   *error)
{
        dbus_bool_t ret;

        _ck_return_val_if_fail (connector != NULL, FALSE);
        _ck_return_val_if_fail (display_device != NULL, FALSE);
        _ck_return_val_if_fail ((error) == NULL || !dbus_error_is_set ((error)), FALSE);

        ret = ck_connector_open_session_with_parameters (connector,
                                                         error,
                                                         "display-device", &display_device,
                                                         "x11-display", &x11_display,
                                                         "unix-user", &user,
                                                         NULL);
        return ret;
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
        _ck_return_val_if_fail (connector != NULL, NULL);

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
dbus_bool_t
ck_connector_close_session (CkConnector *connector,
                            DBusError   *error)
{
        DBusError    local_error;
        DBusMessage *message;
        DBusMessage *reply;
        dbus_bool_t  ret;
        dbus_bool_t  session_closed;

        _ck_return_val_if_fail (connector != NULL, FALSE);
        _ck_return_val_if_fail ((error) == NULL || !dbus_error_is_set ((error)), FALSE);

        reply = NULL;
        message = NULL;
        ret = FALSE;

        if (!connector->session_created || connector->cookie == NULL) {
                dbus_set_error (error,
                                CK_CONNECTOR_ERROR,
                                "Unable to close session: %s",
                                "no session open");
                goto out;
        }

        dbus_error_init (&local_error);
        message = dbus_message_new_method_call ("org.freedesktop.ConsoleKit",
                                                "/org/freedesktop/ConsoleKit/Manager",
                                                "org.freedesktop.ConsoleKit.Manager",
                                                "CloseSession");
        if (message == NULL) {
                goto out;
        }

        if (! dbus_message_append_args (message,
                                        DBUS_TYPE_STRING, &(connector->cookie),
                                        DBUS_TYPE_INVALID)) {
                goto out;
        }

        dbus_error_init (&local_error);
        reply = dbus_connection_send_with_reply_and_block (connector->connection,
                                                           message,
                                                           -1,
                                                           &local_error);
        if (reply == NULL) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to close session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        dbus_error_init (&local_error);
        if (! dbus_message_get_args (reply,
                                     &local_error,
                                     DBUS_TYPE_BOOLEAN, &session_closed,
                                     DBUS_TYPE_INVALID)) {
                if (dbus_error_is_set (&local_error)) {
                        dbus_set_error (error,
                                        CK_CONNECTOR_ERROR,
                                        "Unable to close session: %s",
                                        local_error.message);
                        dbus_error_free (&local_error);
                        goto out;
                }
        }

        if (! session_closed) {
                goto out;
        }

        connector->session_created = FALSE;
        ret = TRUE;

out:
        if (reply != NULL) {
                dbus_message_unref (reply);
        }

        if (message != NULL) {
                dbus_message_unref (message);
        }

        return ret;

}
