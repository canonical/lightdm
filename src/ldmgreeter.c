/*
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Robert Ancell <robert.ancell@canonical.com>
 * 
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See http://www.gnu.org/copyleft/gpl.html the full text of the
 * license.
 */

#include <gtk/gtk.h>

#include "greeter.h"

static Greeter *greeter;
static GtkWidget *window, *vbox, *label, *username_entry, *password_entry;

static void
username_activate_cb (GtkWidget *widget)
{
    greeter_start_authentication (greeter, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
password_activate_cb (GtkWidget *widget)
{
    greeter_provide_secret (greeter, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
show_prompt_cb (Greeter *greeter, const gchar *text)
{
    gtk_widget_set_sensitive (password_entry, TRUE);
    gtk_widget_grab_focus (password_entry);
}

static void
show_message_cb (Greeter *greeter, const gchar *text)
{
    gtk_label_set_text (GTK_LABEL (label), text);
}

static void
authentication_complete_cb (Greeter *greeter)
{
    if (greeter_get_is_authenticated (greeter))
        gtk_main_quit ();

    gtk_label_set_text (GTK_LABEL (label), "Failed to authenticate");
    gtk_entry_set_text (GTK_ENTRY (password_entry), "");
    gtk_widget_grab_focus (username_entry);
    gtk_widget_set_sensitive (password_entry, FALSE);
}

int
main(int argc, char **argv)
{
    gtk_init (&argc, &argv);
  
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width (GTK_CONTAINER (window), 12);
    g_signal_connect (window, "delete-event", gtk_main_quit, NULL);

    vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (window), vbox);

    label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    username_entry = gtk_entry_new ();
    gtk_box_pack_start (GTK_BOX (vbox), username_entry, FALSE, FALSE, 0);
    g_signal_connect (username_entry, "activate", G_CALLBACK (username_activate_cb), NULL);

    password_entry = gtk_entry_new ();
    gtk_widget_set_sensitive (password_entry, FALSE);
    gtk_box_pack_start (GTK_BOX (vbox), password_entry, FALSE, FALSE, 0);
    g_signal_connect (password_entry, "activate", G_CALLBACK (password_activate_cb), NULL);  

    gtk_widget_show_all (window);

    greeter = greeter_new ();
    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
  
    gtk_main ();

    return 0;
}
