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
#include <glib/gi18n.h>

#include "greeter.h"

static Greeter *greeter;
static GtkListStore *session_model, *user_model;
static GtkWidget *user_window, *vbox, *label, *user_view;
static GtkWidget *username_entry, *password_entry;
static GtkWidget *session_combo;
static GtkWidget *panel_window;

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

static void
timed_login_cb (Greeter *greeter, const gchar *username)
{
    gtk_main_quit ();
}

int
main(int argc, char **argv)
{
    const GList *sessions, *users, *link;
    GtkCellRenderer *renderer;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkAllocation allocation;
    GtkWidget *menu_bar, *menu, *item;

    gtk_init (&argc, &argv);
  
    // FIXME: Draw background
    //gdk_get_default_root_window ();

    greeter = greeter_new ();

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);  

    user_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (user_window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (user_window), FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (user_window), 12);
    g_signal_connect (user_window, "delete-event", gtk_main_quit, NULL);

    vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (user_window), vbox);

    label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

    user_model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    users = greeter_get_users (greeter);
    for (link = users; link; link = link->next)
    {
        UserInfo *user = link->data;
        GtkTreeIter iter;
      
        gtk_list_store_append (GTK_LIST_STORE (user_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (user_model), &iter,
                            0, user->name,
                            1, user->real_name[0] != '\0' ? user->real_name : user->name,
                            2, "gnome-calculator",
                            -1);
    }

    user_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (user_model));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (user_view), FALSE);
    gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (user_view), GTK_TREE_VIEW_GRID_LINES_NONE);

    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set (G_OBJECT (renderer), "stock-size", GTK_ICON_SIZE_DIALOG, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 0, "User", renderer, "icon-name", 2, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 1, "User", renderer, "text", 1, NULL);

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

    session_model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    sessions = greeter_get_sessions (greeter);
    for (link = sessions; link; link = link->next)
    {
        Session *session = link->data;
        GtkTreeIter iter;
      
        gtk_list_store_append (GTK_LIST_STORE (session_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (session_model), &iter,
                            0, session->name,
                            1, session->name,
                            -1);
    }

    session_combo = gtk_combo_box_new_with_model (GTK_TREE_MODEL (session_model));
    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (session_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (session_combo), renderer, "text", 1);
    gtk_box_pack_start (GTK_BOX (vbox), session_combo, FALSE, FALSE, 0);    
  
    gtk_widget_show_all (user_window);
  
    /* Center the window */
    gtk_widget_get_allocation (user_window, &allocation);
    gtk_window_move (GTK_WINDOW (user_window),
                     (screen_width - allocation.width) / 2,
                     (screen_height - allocation.height) / 2);

    panel_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (panel_window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (panel_window), FALSE);
    gtk_window_set_default_size (GTK_WINDOW (panel_window), screen_width, 10);

    menu_bar = gtk_menu_bar_new ();
    gtk_container_add (GTK_CONTAINER (panel_window), menu_bar);

    item = gtk_image_menu_item_new ();
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), gtk_image_new_from_icon_name ("access", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_menu_item_set_label (GTK_MENU_ITEM (item), ""); // NOTE: Needed to make the icon show as selected
    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), item);

    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?1"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?2"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?3"));

    item = gtk_menu_item_new_with_label (_("Options"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), item);

    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label (_("Select Language...")));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label (_("Select Keyboard Layout...")));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label (_("Select Session...")));

    item = gtk_image_menu_item_new ();
    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
    gtk_menu_item_set_right_justified (GTK_MENU_ITEM (item), TRUE);
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_menu_item_set_label (GTK_MENU_ITEM (item), ""); // NOTE: Needed to make the icon show as selected
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), item);
  
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), menu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("Suspend"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("Hibernate"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("Restart..."));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("Shutdown..."));

    gtk_widget_show_all (panel_window);

    gtk_widget_get_allocation (panel_window, &allocation);
    gtk_widget_set_size_request (GTK_WIDGET (panel_window), screen_width, allocation.height);  
    gtk_window_move (GTK_WINDOW (panel_window), 0, screen_height - allocation.height);

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), NULL);

    greeter_connect (greeter);

    gtk_main ();

    return 0;
}
