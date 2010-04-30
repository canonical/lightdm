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
static GtkListStore *user_model;
static GtkWidget *window, *vbox, *label, *user_view, *username_entry, *password_entry;

static void
user_view_activate_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column)
{
    GtkTreeIter iter;
    gchar *user;
    gtk_tree_model_get_iter (GTK_TREE_MODEL (user_model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (user_model), &iter, 0, &user, -1);

    gtk_entry_set_text (GTK_ENTRY (username_entry), user);
    greeter_start_authentication (greeter, user);

    g_free (user);
}

static void
username_activate_cb (GtkWidget *widget)
{
    greeter_start_authentication (greeter, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
password_activate_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (widget, FALSE);
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
}

int
main(int argc, char **argv)
{
    const GList *users, *link;
    GtkCellRenderer *renderer;

    gtk_init (&argc, &argv);

    greeter = greeter_new ();
  
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width (GTK_CONTAINER (window), 12);
    g_signal_connect (window, "delete-event", gtk_main_quit, NULL);

    vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (window), vbox);

    label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    user_model = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    users = greeter_get_users (greeter);
    for (link = users; link; link = link->next)
    {
        UserInfo *user = link->data;
        GtkTreeIter iter;

        gtk_list_store_append (GTK_LIST_STORE (user_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (user_model), &iter, 0, user->name, 1, user->real_name ? user->real_name : user->name, -1);
    }

    user_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (user_model));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (user_view), FALSE);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 0, "User", renderer, "text", 1, NULL);
    gtk_box_pack_start (GTK_BOX (vbox), user_view, FALSE, FALSE, 0);
    g_signal_connect (user_view, "row-activated", G_CALLBACK (user_view_activate_cb), NULL);

    username_entry = gtk_entry_new ();
    gtk_box_pack_start (GTK_BOX (vbox), username_entry, FALSE, FALSE, 0);
    g_signal_connect (username_entry, "activate", G_CALLBACK (username_activate_cb), NULL);

    password_entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY (password_entry), FALSE);
    gtk_widget_set_sensitive (password_entry, FALSE);
    gtk_box_pack_start (GTK_BOX (vbox), password_entry, FALSE, FALSE, 0);
    g_signal_connect (password_entry, "activate", G_CALLBACK (password_activate_cb), NULL);  

    gtk_widget_show_all (window);

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);

    greeter_connect (greeter);

    gtk_main ();

    return 0;
}
