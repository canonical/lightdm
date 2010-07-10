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

static LdmGreeter *greeter;
static GtkListStore *user_model;
static GtkWidget *user_window, *vbox, *label, *user_view;
static GtkWidget *username_entry, *password_entry;
static GtkWidget *panel_window;

static void
user_view_activate_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column)
{
    GtkTreeIter iter;
    gchar *user;

    gtk_tree_model_get_iter (GTK_TREE_MODEL (user_model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (user_model), &iter, 0, &user, -1);

    gtk_entry_set_text (GTK_ENTRY (username_entry), user);
    ldm_greeter_start_authentication (greeter, user);

    g_free (user);
}

static void
username_activate_cb (GtkWidget *widget)
{
    ldm_greeter_start_authentication (greeter, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
password_activate_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (widget, FALSE);
    ldm_greeter_provide_secret (greeter, gtk_entry_get_text (GTK_ENTRY (widget)));
}

static void
show_prompt_cb (LdmGreeter *greeter, const gchar *text)
{
    gtk_widget_show (password_entry);
    gtk_widget_set_sensitive (password_entry, TRUE);
    gtk_widget_grab_focus (password_entry);
}

static void
show_message_cb (LdmGreeter *greeter, const gchar *text)
{
    gtk_widget_show (label);
    gtk_label_set_text (GTK_LABEL (label), text);
}

static void
authentication_complete_cb (LdmGreeter *greeter)
{
    if (ldm_greeter_get_is_authenticated (greeter))
    {
        ldm_greeter_login (greeter);
    }
    else
    {
        gtk_widget_show (label);
        gtk_label_set_text (GTK_LABEL (label), "Failed to authenticate");
        gtk_entry_set_text (GTK_ENTRY (password_entry), "");
        gtk_widget_grab_focus (username_entry);
    }
}

static void
timed_login_cb (LdmGreeter *greeter, const gchar *username)
{
    ldm_greeter_login (greeter);
}

static void
quit_cb (LdmGreeter *greeter, const gchar *username)
{
    gtk_main_quit ();
}

static void
layout_changed_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        ldm_greeter_set_layout (greeter, g_object_get_data (G_OBJECT (widget), "layout"));
}

static void
session_changed_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        ldm_greeter_set_session (greeter, g_object_get_data (G_OBJECT (widget), "key"));
}

int
main(int argc, char **argv)
{
    GdkWindow *root;
    const GList *items, *item;
    GSList *session_radio_list = NULL, *layout_radio_list = NULL;
    GtkCellRenderer *renderer;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkAllocation allocation;
    GtkWidget *option_menu, *power_menu;
    GtkWidget *menu_bar, *menu, *menu_item;
    GdkColor background_color;
    gint n_power_items = 0;

    gtk_init (&argc, &argv);

    greeter = ldm_greeter_new ();

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "quit", G_CALLBACK (quit_cb), NULL);

    ldm_greeter_connect (greeter);

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);
  
    root = gdk_get_default_root_window ();
    gdk_window_set_cursor (root, gdk_cursor_new (GDK_LEFT_PTR));
    gdk_color_parse ("#000000", &background_color);
    gdk_color_alloc (gdk_window_get_colormap (root), &background_color);
    gdk_window_set_back_pixmap (root, NULL, TRUE);
    gdk_window_set_background (root, &background_color);
    gdk_window_clear(root);

    user_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (user_window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (user_window), FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (user_window), 12);
    g_signal_connect (user_window, "delete-event", gtk_main_quit, NULL);

    vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (user_window), vbox);

    label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (label, TRUE);    

    user_model = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF);
    items = ldm_greeter_get_users (greeter);
    for (item = items; item; item = item->next)
    {
        LdmUser *user = item->data;
        GtkTreeIter iter;
        const gchar *image;
        GdkPixbuf *pixbuf = NULL;

        image = ldm_user_get_image (user);
        if (image[0] != '\0')
        {
            gchar *path;

            path = g_filename_from_uri (image, NULL, NULL);
            if (path)
                pixbuf = gdk_pixbuf_new_from_file_at_scale (path, 64, 64, TRUE, NULL);
            g_free (path);
        }
        if (!pixbuf)
            pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
                                               "stock_person",
                                               64,
                                               0,
                                               NULL);
      
        gtk_list_store_append (GTK_LIST_STORE (user_model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (user_model), &iter,
                            0, ldm_user_get_name (user),
                            1, ldm_user_get_display_name (user),
                            2, pixbuf,
                            -1);
    }

    user_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (user_model));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (user_view), FALSE);
    gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (user_view), GTK_TREE_VIEW_GRID_LINES_NONE);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 0, "User", renderer, "pixbuf", 2, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 1, "User", renderer, "text", 1, NULL);

    gtk_box_pack_start (GTK_BOX (vbox), user_view, FALSE, FALSE, 0);
    g_signal_connect (user_view, "row-activated", G_CALLBACK (user_view_activate_cb), NULL);

    username_entry = gtk_entry_new ();
    gtk_box_pack_start (GTK_BOX (vbox), username_entry, FALSE, FALSE, 0);
    g_signal_connect (username_entry, "activate", G_CALLBACK (username_activate_cb), NULL);
    gtk_widget_set_no_show_all (username_entry, TRUE);

    password_entry = gtk_entry_new ();
    //gtk_entry_set_visibility (GTK_ENTRY (password_entry), FALSE);
    gtk_widget_set_sensitive (password_entry, FALSE);
    gtk_box_pack_start (GTK_BOX (vbox), password_entry, FALSE, FALSE, 0);
    g_signal_connect (password_entry, "activate", G_CALLBACK (password_activate_cb), NULL);
    gtk_widget_set_no_show_all (password_entry, TRUE);

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

    menu_item = gtk_image_menu_item_new ();
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), gtk_image_new_from_icon_name ("access", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_menu_item_set_label (GTK_MENU_ITEM (menu_item), ""); // NOTE: Needed to make the icon show as selected
    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menu_item), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);

    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?1"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?2"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), gtk_menu_item_new_with_label ("?3"));

    menu_item = gtk_menu_item_new_with_label (_("Options"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
    option_menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), option_menu);

    menu_item = gtk_menu_item_new_with_label (_("Language"));
    gtk_menu_shell_append (GTK_MENU_SHELL (option_menu), menu_item);
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

    menu_item = gtk_menu_item_new_with_label (_("Keyboard Layout"));
    gtk_menu_shell_append (GTK_MENU_SHELL (option_menu), menu_item);
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
    items = ldm_greeter_get_layouts (greeter);
    for (item = items; item; item = item->next)
    {
        LdmLayout *layout = item->data;

        menu_item = gtk_radio_menu_item_new_with_label (layout_radio_list, ldm_layout_get_description (layout));
        layout_radio_list = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menu_item));
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

        if (g_str_equal (ldm_layout_get_name (layout), ldm_greeter_get_layout (greeter)))
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);

        g_object_set_data (G_OBJECT (menu_item), "layout", g_strdup (ldm_layout_get_name (layout)));
        g_signal_connect (menu_item, "toggled", G_CALLBACK (layout_changed_cb), NULL);
    }

    menu_item = gtk_menu_item_new_with_label (_("Session"));
    gtk_menu_shell_append (GTK_MENU_SHELL (option_menu), menu_item);
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
    items = ldm_greeter_get_sessions (greeter);
    for (item = items; item; item = item->next)
    {
        LdmSession *session = item->data;

        menu_item = gtk_radio_menu_item_new_with_label (session_radio_list, ldm_session_get_name (session));
        session_radio_list = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menu_item));
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

        if (g_str_equal (ldm_session_get_key (session), ldm_greeter_get_session (greeter)))
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);

        g_object_set_data (G_OBJECT (menu_item), "key", g_strdup (ldm_session_get_key (session)));
        g_signal_connect (menu_item, "toggled", G_CALLBACK (session_changed_cb), NULL);
    }

    power_menu = gtk_menu_new ();
    if (ldm_greeter_get_can_suspend (greeter))
    {
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), gtk_menu_item_new_with_label ("Suspend"));
        n_power_items++;
    }
    if (ldm_greeter_get_can_hibernate (greeter))
    {
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), gtk_menu_item_new_with_label ("Hibernate"));
        n_power_items++;
    }
    if (ldm_greeter_get_can_restart (greeter))
    {
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), gtk_menu_item_new_with_label ("Restart..."));
        n_power_items++;
    }
    if (ldm_greeter_get_can_shutdown (greeter))
    {
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), gtk_menu_item_new_with_label ("Shutdown..."));
        n_power_items++;
    }
    if (n_power_items > 0)
    {
        menu_item = gtk_image_menu_item_new ();
        gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menu_item), TRUE);
        gtk_menu_item_set_right_justified (GTK_MENU_ITEM (menu_item), TRUE);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_LARGE_TOOLBAR));
        gtk_menu_item_set_label (GTK_MENU_ITEM (menu_item), ""); // NOTE: Needed to make the icon show as selected
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
    }

    gtk_widget_show_all (panel_window);

    gtk_widget_get_allocation (panel_window, &allocation);
    gtk_widget_set_size_request (GTK_WIDGET (panel_window), screen_width, allocation.height);  
    gtk_window_move (GTK_WINDOW (panel_window), 0, screen_height - allocation.height);

    gtk_main ();

    return 0;
}
