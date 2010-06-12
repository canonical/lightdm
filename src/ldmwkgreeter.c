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

static Greeter *greeter;

static void
show_prompt_cb (Greeter *greeter, const gchar *text)
{
}

static void
show_message_cb (Greeter *greeter, const gchar *text)
{
}

static void
authentication_complete_cb (Greeter *greeter)
{
    if (greeter_get_is_authenticated (greeter))
        gtk_main_quit ();

}

static void
timed_login_cb (Greeter *greeter, const gchar *username)
{
    gtk_main_quit ();
}

static void
ldm_init_cb (JSContextRef ctx, JSObjectRef object)
{
}

static void
ldm_finalize_cb (JSObjectRef object)
{
}

static JSValueRef
test_cb (JSContextRef context,
         JSObjectRef function,
         JSObjectRef thisObject,
         size_t argumentCount,
         const JSValueRef arguments[],
         JSValueRef *exception)
{
    return JSValueMakeString (context, JSStringCreateWithUTF8CString ("Hello World!"));
}

static JSValueRef
start_authentication_cb (JSContextRef context,
                         JSObjectRef function,
                         JSObjectRef thisObject,
                         size_t argumentCount,
                         const JSValueRef arguments[],
                         JSValueRef *exception)
{
    printf ("!\n");
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
    return JSValueMakeNull (context);
}

static const JSStaticFunction ldm_functions[] =
{
    { "test", test_cb, kJSPropertyAttributeReadOnly },  
    { "start_authentication", start_authentication_cb, kJSPropertyAttributeReadOnly },
    { "provide_secret", provide_secret_cb, kJSPropertyAttributeReadOnly },
    { NULL, NULL, 0 }
};

static const JSClassDefinition ldm_definition =
{
    0,
    kJSClassAttributeNone,
    "LightDMClass",
    NULL,

    NULL,
    ldm_functions,

    ldm_init_cb,
    ldm_finalize_cb
};

static void
window_object_cleared_cb (WebKitWebView  *web_view,
                          WebKitWebFrame *frame)
{
    JSGlobalContextRef context;
    JSClassRef ldm_class;
    JSObjectRef ldm_object;

    context = webkit_web_frame_get_global_context (frame);

    ldm_class = JSClassCreate (&ldm_definition);
    ldm_object = JSObjectMake (context, ldm_class, context);
    JSObjectSetProperty (context,
                         JSContextGetGlobalObject (context),
                         JSStringCreateWithUTF8CString ("lightdm"),
                         ldm_object, kJSPropertyAttributeNone, NULL);
}

int
main(int argc, char **argv)
{
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkWidget *window, *web_view;

    gtk_init (&argc, &argv);

    greeter = greeter_new ();

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), NULL);

    greeter_connect (greeter);

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
    gtk_window_set_default_size (GTK_WINDOW (window), screen_width, screen_height);
    gtk_window_move (GTK_WINDOW (window), 0, 0);

    web_view = webkit_web_view_new ();
    g_signal_connect (G_OBJECT (greeter), "window-object-cleared", G_CALLBACK (window_object_cleared_cb), NULL);
    webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), "file:///home/bob/bzr/lightdm/index.html");
    gtk_container_add (GTK_CONTAINER (window), web_view);

    gtk_widget_show_all (window);

    gtk_main ();

    return 0;
}
