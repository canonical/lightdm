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

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <JavaScriptCore/JavaScript.h>
#include <glib/gi18n.h>

#include "greeter.h"

static void
show_prompt_cb (Greeter *greeter, const gchar *text, WebKitWebView *view)
{
    gchar *command;

    command = g_strdup_printf ("show_prompt('%s')", text); // FIXME: Escape text
    webkit_web_view_execute_script (view, command);
    g_free (command);
}

static void
show_message_cb (Greeter *greeter, const gchar *text, WebKitWebView *view)
{
    gchar *command;

    command = g_strdup_printf ("show_message('%s')", text); // FIXME: Escape text
    webkit_web_view_execute_script (view, command);
    g_free (command);
}

static void
authentication_complete_cb (Greeter *greeter, WebKitWebView *view)
{
    if (greeter_get_is_authenticated (greeter))
        gtk_main_quit ();

}

static void
timed_login_cb (Greeter *greeter, const gchar *username)
{
    gtk_main_quit ();
}

static JSValueRef
start_authentication_cb (JSContextRef context,
                         JSObjectRef function,
                         JSObjectRef thisObject,
                         size_t argumentCount,
                         const JSValueRef arguments[],
                         JSValueRef *exception)
{
    Greeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef name_arg;
    char name[1024];

    // FIXME: Throw exception
    if (!(argumentCount == 1 && JSValueGetType (context, arguments[0]) == kJSTypeString))
        return JSValueMakeNull (context);

    name_arg = JSValueToStringCopy (context, arguments[0], NULL);
    JSStringGetUTF8CString (name_arg, name, 1024);
    JSStringRelease (name_arg);

    greeter_start_authentication (greeter, name);
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
    Greeter *greeter = JSObjectGetPrivate (thisObject);
    JSStringRef secret_arg;
    char secret[1024];

    // FIXME: Throw exception
    if (!(argumentCount == 1 && JSValueGetType (context, arguments[0]) == kJSTypeString))
        return JSValueMakeNull (context);

    secret_arg = JSValueToStringCopy (context, arguments[0], NULL);
    JSStringGetUTF8CString (secret_arg, secret, 1024);
    JSStringRelease (secret_arg);

    greeter_provide_secret (greeter, secret);
    return JSValueMakeNull (context);
}

static const JSStaticFunction ldm_functions[] =
{
    { "start_authentication", start_authentication_cb, kJSPropertyAttributeReadOnly },
    { "provide_secret", provide_secret_cb, kJSPropertyAttributeReadOnly },
    { NULL, NULL, 0 }
};

static const JSClassDefinition ldm_definition =
{
    0,                     /* Version */
    kJSClassAttributeNone, /* Attributes */
    "LightDMClass",        /* Class name */
    NULL,                  /* Parent class */
    NULL,                  /* Static values */
    ldm_functions,         /* Static functions */
};

static void
window_object_cleared_cb (WebKitWebView  *web_view,
                          WebKitWebFrame *frame,
                          JSGlobalContextRef context,
                          JSObjectRef window_object,
                          Greeter *greeter)
{
    JSClassRef ldm_class;
    JSObjectRef ldm_object;

    ldm_class = JSClassCreate (&ldm_definition);
    ldm_object = JSObjectMake (context, ldm_class, greeter);
    JSObjectSetProperty (context,
                         JSContextGetGlobalObject (context),
                         JSStringCreateWithUTF8CString ("lightdm"),
                         ldm_object, kJSPropertyAttributeNone, NULL);
}

int
main(int argc, char **argv)
{
    Greeter *greeter;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkWidget *window, *web_view;

    gtk_init (&argc, &argv);

    greeter = greeter_new ();

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

    webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), "file:///home/bob/bzr/lightdm/index.html");
    greeter_connect (greeter);

    gtk_widget_show_all (window);

    gtk_main ();

    return 0;
}
