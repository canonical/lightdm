/*
 * Copyright (C) 2010 Robert Ancell.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <stdlib.h>
#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include <glib/gi18n.h>

#include "greeter.h"

static JSClassRef ldm_class, ldm_user_class, ldm_session_class;

static void
show_prompt_cb (LdmGreeter *greeter, const gchar *text, WebKitWebView *view)
{
    gchar *command;

    command = g_strdup_printf ("show_prompt('%s')", text); // FIXME: Escape text
    webkit_web_view_execute_script (view, command);
    g_free (command);
}

static void
show_message_cb (LdmGreeter *greeter, const gchar *text, WebKitWebView *view)
{
    gchar *command;

    command = g_strdup_printf ("show_message('%s')", text); // FIXME: Escape text
    webkit_web_view_execute_script (view, command);
    g_free (command);
}

static void
authentication_complete_cb (LdmGreeter *greeter, WebKitWebView *view)
{
    webkit_web_view_execute_script (view, "authentication_complete()");
}

static void
timed_login_cb (LdmGreeter *greeter, const gchar *username)
{
    gtk_main_quit ();
}

static JSValueRef
get_user_name_cb (JSContextRef context,
                  JSObjectRef thisObject,
                  JSStringRef propertyName,
                  JSValueRef *exception)
{
    LdmUser *user = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_user_get_name (user));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_user_real_name_cb (JSContextRef context,
                       JSObjectRef thisObject,
                       JSStringRef propertyName,
                       JSValueRef *exception)
{
    LdmUser *user = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_user_get_real_name (user));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_user_image_cb (JSContextRef context,
                   JSObjectRef thisObject,
                   JSStringRef propertyName,
                   JSValueRef *exception)
{
    LdmUser *user = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_user_get_image (user));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_user_logged_in_cb (JSContextRef context,
                       JSObjectRef thisObject,
                       JSStringRef propertyName,
                       JSValueRef *exception)
{
    LdmUser *user = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_user_get_logged_in (user));
}

static JSValueRef
get_session_key_cb (JSContextRef context,
                    JSObjectRef thisObject,
                    JSStringRef propertyName,
                    JSValueRef *exception)
{
    LdmSession *session = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_session_get_key (session));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_session_name_cb (JSContextRef context,
                     JSObjectRef thisObject,
                     JSStringRef propertyName,
                     JSValueRef *exception)
{
    LdmSession *session = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_session_get_name (session));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_session_comment_cb (JSContextRef context,
                        JSObjectRef thisObject,
                        JSStringRef propertyName,
                        JSValueRef *exception)
{
    LdmSession *session = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_session_get_comment (session));
    return JSValueMakeString (context, string);
}

static JSValueRef
get_num_users_cb (JSContextRef context,
                  JSObjectRef thisObject,
                  JSStringRef propertyName,
                  JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    gint num_users;
  
    num_users = ldm_greeter_get_num_users (greeter);
    return JSValueMakeNumber (context, num_users);
}

static JSValueRef
get_users_cb (JSContextRef context,
              JSObjectRef thisObject,
              JSStringRef propertyName,
              JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSObjectRef array;
    const GList *users, *link;
    guint i, n_users = 0;
    JSValueRef *args;
  
    users = ldm_greeter_get_users (greeter);
    n_users = g_list_length ((GList *)users);
    args = g_malloc (sizeof (JSValueRef) * (n_users + 1));
    for (i = 0, link = users; link; i++, link = link->next)
    {
        LdmUser *user = link->data;
        g_object_ref (user);
        args[i] = JSObjectMake (context, ldm_user_class, user);
    }

    array = JSObjectMakeArray (context, n_users, args, NULL);
    g_free (args);
    return array;
}

static JSValueRef
get_sessions_cb (JSContextRef context,
                 JSObjectRef thisObject,
                 JSStringRef propertyName,
                 JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSObjectRef array;
    const GList *sessions, *link;
    guint i, n_sessions = 0;
    JSValueRef *args;
  
    sessions = ldm_greeter_get_sessions (greeter);
    n_sessions = g_list_length ((GList *)sessions);
    args = g_malloc (sizeof (JSValueRef) * (n_sessions + 1));
    for (i = 0, link = sessions; link; i++, link = link->next)
    {
        LdmSession *session = link->data;
        g_object_ref (session);
        args[i] = JSObjectMake (context, ldm_session_class, session);
    }

    array = JSObjectMakeArray (context, n_sessions, args, NULL);
    g_free (args);
    return array;
}

static JSValueRef
get_session_cb (JSContextRef context,
                JSObjectRef thisObject,
                JSStringRef propertyName,
                JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_greeter_get_session (greeter));

    return JSValueMakeString (context, string);
}

static bool
set_session_cb (JSContextRef context,
                JSObjectRef thisObject,
                JSStringRef propertyName,
                JSValueRef value,
                JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);  
    JSStringRef session_arg;
    char session[1024];

    // FIXME: Throw exception
    if (JSValueGetType (context, value) != kJSTypeString)
        return false;

    session_arg = JSValueToStringCopy (context, value, NULL);
    JSStringGetUTF8CString (session_arg, session, 1024);
    JSStringRelease (session_arg);
  
    ldm_greeter_set_session (greeter, session);

    return true;
}

static JSValueRef
get_timed_login_user_cb (JSContextRef context,
                         JSObjectRef thisObject,
                         JSStringRef propertyName,
                         JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef string;

    string = JSStringCreateWithUTF8CString (ldm_greeter_get_timed_login_user (greeter));

    return JSValueMakeString (context, string);
}

static JSValueRef
get_timed_login_delay_cb (JSContextRef context,
                          JSObjectRef thisObject,
                          JSStringRef propertyName,
                          JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    gint delay;
  
    delay = ldm_greeter_get_timed_login_delay (greeter);
    return JSValueMakeNumber (context, delay);
}

static JSValueRef
cancel_timed_login_cb (JSContextRef context,
                       JSObjectRef function,
                       JSObjectRef thisObject,
                       size_t argumentCount,
                       const JSValueRef arguments[],
                       JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);
  
    ldm_greeter_cancel_timed_login (greeter);
    return JSValueMakeNull (context);
}

static JSValueRef
start_authentication_cb (JSContextRef context,
                         JSObjectRef function,
                         JSObjectRef thisObject,
                         size_t argumentCount,
                         const JSValueRef arguments[],
                         JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef name_arg;
    char name[1024];

    // FIXME: Throw exception
    if (!(argumentCount == 1 && JSValueGetType (context, arguments[0]) == kJSTypeString))
        return JSValueMakeNull (context);

    name_arg = JSValueToStringCopy (context, arguments[0], NULL);
    JSStringGetUTF8CString (name_arg, name, 1024);
    JSStringRelease (name_arg);

    ldm_greeter_start_authentication (greeter, name);
    return JSValueMakeNull (context);
}

static JSValueRef
provide_secret_cb (JSContextRef context,
                   JSObjectRef function,
                   JSObjectRef thisObject,
                   size_t argumentCount,
                   const JSValueRef arguments[],
                   JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef secret_arg;
    char secret[1024];

    // FIXME: Throw exception
    if (!(argumentCount == 1 && JSValueGetType (context, arguments[0]) == kJSTypeString))
        return JSValueMakeNull (context);

    secret_arg = JSValueToStringCopy (context, arguments[0], NULL);
    JSStringGetUTF8CString (secret_arg, secret, 1024);
    JSStringRelease (secret_arg);

    ldm_greeter_provide_secret (greeter, secret);
    return JSValueMakeNull (context);
}

static JSValueRef
cancel_authentication_cb (JSContextRef context,
                          JSObjectRef function,
                          JSObjectRef thisObject,
                          size_t argumentCount,
                          const JSValueRef arguments[],
                          JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    ldm_greeter_cancel_authentication (greeter);
    return JSValueMakeNull (context);
}

static JSValueRef
get_is_authenticated_cb (JSContextRef context,
                         JSObjectRef thisObject,
                         JSStringRef propertyName,
                         JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_greeter_get_is_authenticated (greeter));
}

static JSValueRef
get_can_suspend_cb (JSContextRef context,
                    JSObjectRef thisObject,
                    JSStringRef propertyName,
                    JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_greeter_get_can_suspend (greeter));
}

static JSValueRef
suspend_cb (JSContextRef context,
            JSObjectRef function,
            JSObjectRef thisObject,
            size_t argumentCount,
            const JSValueRef arguments[],
            JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    ldm_greeter_suspend (greeter);
    return JSValueMakeNull (context);
}

static JSValueRef
get_can_hibernate_cb (JSContextRef context,
                      JSObjectRef thisObject,
                      JSStringRef propertyName,
                      JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_greeter_get_can_hibernate (greeter));  
}

static JSValueRef
hibernate_cb (JSContextRef context,
              JSObjectRef function,
              JSObjectRef thisObject,
              size_t argumentCount,
              const JSValueRef arguments[],
              JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    ldm_greeter_hibernate (greeter);
    return JSValueMakeNull (context);
}

static JSValueRef
get_can_restart_cb (JSContextRef context,
                    JSObjectRef thisObject,
                    JSStringRef propertyName,
                    JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_greeter_get_can_restart (greeter));
}

static JSValueRef
restart_cb (JSContextRef context,
            JSObjectRef function,
            JSObjectRef thisObject,
            size_t argumentCount,
            const JSValueRef arguments[],
            JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    ldm_greeter_restart (greeter);
    return JSValueMakeNull (context);
}

static JSValueRef
get_can_shutdown_cb (JSContextRef context,
                     JSObjectRef thisObject,
                     JSStringRef propertyName,
                     JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);
    return JSValueMakeBoolean (context, ldm_greeter_get_can_shutdown (greeter));
}

static JSValueRef
shutdown_cb (JSContextRef context,
             JSObjectRef function,
             JSObjectRef thisObject,
             size_t argumentCount,
             const JSValueRef arguments[],
             JSValueRef *exception)
{
    LdmGreeter *greeter = JSObjectGetPrivate (thisObject);

    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    ldm_greeter_shutdown (greeter);  
    return JSValueMakeNull (context);
}

static JSValueRef
close_cb (JSContextRef context,
          JSObjectRef function,
          JSObjectRef thisObject,
          size_t argumentCount,
          const JSValueRef arguments[],
          JSValueRef *exception)
{
    // FIXME: Throw exception
    if (argumentCount != 0)
        return JSValueMakeNull (context);

    exit (0);
}

static const JSStaticValue ldm_user_values[] =
{
    { "name", get_user_name_cb, NULL, kJSPropertyAttributeReadOnly },
    { "real_name", get_user_real_name_cb, NULL, kJSPropertyAttributeReadOnly },
    { "image", get_user_image_cb, NULL, kJSPropertyAttributeReadOnly },
    { "logged_in", get_user_logged_in_cb, NULL, kJSPropertyAttributeReadOnly },
    { NULL, NULL, NULL, 0 }
};

static const JSStaticValue ldm_session_values[] =
{
    { "key", get_session_key_cb, NULL, kJSPropertyAttributeReadOnly },
    { "name", get_session_name_cb, NULL, kJSPropertyAttributeReadOnly },
    { "comment", get_session_comment_cb, NULL, kJSPropertyAttributeReadOnly },
    { NULL, NULL, NULL, 0 }
};

static const JSStaticValue ldm_values[] =
{
    { "users", get_users_cb, NULL, kJSPropertyAttributeReadOnly },
    { "sessions", get_sessions_cb, NULL, kJSPropertyAttributeReadOnly },
    { "num_users", get_num_users_cb, NULL, kJSPropertyAttributeReadOnly },
    { "session", get_session_cb, set_session_cb, kJSPropertyAttributeNone },
    { "timed_login_user", get_timed_login_user_cb, NULL, kJSPropertyAttributeReadOnly },  
    { "timed_login_delay", get_timed_login_delay_cb, NULL, kJSPropertyAttributeReadOnly },
    { "is_authenticated", get_is_authenticated_cb, NULL, kJSPropertyAttributeReadOnly },
    { "can_suspend", get_can_suspend_cb, NULL, kJSPropertyAttributeReadOnly },
    { "can_hibernate", get_can_hibernate_cb, NULL, kJSPropertyAttributeReadOnly },
    { "can_restart", get_can_restart_cb, NULL, kJSPropertyAttributeReadOnly },
    { "can_shutdown", get_can_shutdown_cb, NULL, kJSPropertyAttributeReadOnly },
    { NULL, NULL, NULL, 0 }
};

static const JSStaticFunction ldm_functions[] =
{
    { "cancel_timed_login", cancel_timed_login_cb, kJSPropertyAttributeReadOnly },  
    { "start_authentication", start_authentication_cb, kJSPropertyAttributeReadOnly },
    { "provide_secret", provide_secret_cb, kJSPropertyAttributeReadOnly },
    { "cancel_authentication", cancel_authentication_cb, kJSPropertyAttributeReadOnly },
    { "suspend", suspend_cb, kJSPropertyAttributeReadOnly },
    { "hibernate", hibernate_cb, kJSPropertyAttributeReadOnly },
    { "restart", restart_cb, kJSPropertyAttributeReadOnly },
    { "shutdown", shutdown_cb, kJSPropertyAttributeReadOnly },
    { "close", close_cb, kJSPropertyAttributeReadOnly },
    { NULL, NULL, 0 }
};

static const JSClassDefinition ldm_user_definition =
{
    0,                     /* Version */
    kJSClassAttributeNone, /* Attributes */
    "LightDMUser",         /* Class name */
    NULL,                  /* Parent class */
    ldm_user_values,       /* Static values */
};

static const JSClassDefinition ldm_session_definition =
{
    0,                     /* Version */
    kJSClassAttributeNone, /* Attributes */
    "LightDMSession",      /* Class name */
    NULL,                  /* Parent class */
    ldm_session_values,    /* Static values */
};

static const JSClassDefinition ldm_definition =
{
    0,                     /* Version */
    kJSClassAttributeNone, /* Attributes */
    "LightDMClass",        /* Class name */
    NULL,                  /* Parent class */
    ldm_values,            /* Static values */
    ldm_functions,         /* Static functions */
};

static void
window_object_cleared_cb (WebKitWebView  *web_view,
                          WebKitWebFrame *frame,
                          JSGlobalContextRef context,
                          JSObjectRef window_object,
                          LdmGreeter *greeter)
{
    JSObjectRef ldm_object;

    ldm_class = JSClassCreate (&ldm_definition);
    ldm_user_class = JSClassCreate (&ldm_user_definition);
    ldm_session_class = JSClassCreate (&ldm_session_definition);

    ldm_object = JSObjectMake (context, ldm_class, greeter);
    JSObjectSetProperty (context,
                         JSContextGetGlobalObject (context),
                         JSStringCreateWithUTF8CString ("lightdm"),
                         ldm_object, kJSPropertyAttributeNone, NULL);
}

int
main(int argc, char **argv)
{
    LdmGreeter *greeter;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkWidget *window, *web_view;
    gchar *url;

    gtk_init (&argc, &argv);
  
    if (argc != 2) {
        g_printerr ("Usage: %s <url>\n", argv[0]);
        return 1;
    }
    url = argv[1];

    greeter = ldm_greeter_new ();

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
    gtk_window_set_default_size (GTK_WINDOW (window), screen_width, screen_height);
    gtk_window_move (GTK_WINDOW (window), 0, 0);

    web_view = webkit_web_view_new ();
    g_signal_connect (G_OBJECT (web_view), "window-object-cleared", G_CALLBACK (window_object_cleared_cb), greeter);
    gtk_container_add (GTK_CONTAINER (window), web_view);

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), web_view);
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), web_view);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), web_view);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), web_view);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), web_view);

    webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), url);
    ldm_greeter_connect (greeter);

    gtk_widget_show_all (window);

    gtk_main ();

    return 0;
}
