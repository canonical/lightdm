/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * ck-connector.h : Code for login managers to register with ConsoleKit.
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

#ifndef CK_CONNECTOR_H
#define CK_CONNECTOR_H

#include <sys/types.h>
#include <dbus/dbus.h>

DBUS_BEGIN_DECLS

struct _CkConnecter;
typedef struct _CkConnector CkConnector;

CkConnector  *ck_connector_new                          (void);

CkConnector  *ck_connector_ref                          (CkConnector *ckc);
void          ck_connector_unref                        (CkConnector *ckc);

dbus_bool_t   ck_connector_open_session_for_user        (CkConnector *ckc,
                                                         uid_t        user,
                                                         const char  *tty,
                                                         const char  *x11_display,
                                                         DBusError   *error);
dbus_bool_t   ck_connector_open_session_with_parameters (CkConnector *ckc,
                                                         DBusError   *error,
                                                         const char  *first_parameter_name,
                                                         ...);
dbus_bool_t   ck_connector_open_session                 (CkConnector *ckc,
                                                         DBusError   *error);

const char   *ck_connector_get_cookie                   (CkConnector *ckc);
dbus_bool_t   ck_connector_close_session                (CkConnector *ckc,
                                                         DBusError   *error);

DBUS_END_DECLS

#endif /* CK_CONNECTOR_H */
