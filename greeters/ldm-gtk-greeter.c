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
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>

#include "greeter.h"

static LdmGreeter *greeter;
static GtkTreeModel *user_model;
static GtkWidget *user_window, *vbox, *message_label, *user_view;
static GtkWidget *username_entry, *password_entry;
static GtkWidget *panel_window;
static gchar *session = NULL, *theme_name;

static void
start_authentication (const gchar *username)
{
    GtkTreeIter iter;

    if (user_model && gtk_tree_model_get_iter_first (GTK_TREE_MODEL (user_model), &iter))
    {
        do
        {
            gchar *user;
            gtk_tree_model_get (GTK_TREE_MODEL (user_model), &iter, 0, &user, -1);
            gtk_list_store_set (GTK_LIST_STORE (user_model), &iter, 3, strcmp (user, username) == 0, -1);
            g_free (user);
        } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (user_model), &iter));
    }
    if (username_entry)
        gtk_widget_set_sensitive (username_entry, FALSE);

    ldm_greeter_start_authentication (greeter, username);
}

static void
user_view_activate_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column)
{
    GtkTreeIter iter;
    gchar *user;

    gtk_tree_model_get_iter (GTK_TREE_MODEL (user_model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (user_model), &iter, 0, &user, -1);
    start_authentication (user);
    g_free (user);
}

static gboolean
idle_select_cb ()
{
    GtkTreeIter iter;
    gchar *user;

    if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (user_view)),
                                         NULL, &iter))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (user_model), &iter, 0, &user, -1);
        start_authentication (user);
        g_free (user);
    }

    return FALSE;
}

static gboolean
user_view_click_cb (GtkWidget *widget, GdkEventButton *event)
{
    /* Do it in the idle loop so the selection is done first */
    g_idle_add (idle_select_cb, NULL);
    return FALSE;
}

static void
username_activate_cb (GtkWidget *widget)
{
    start_authentication (gtk_entry_get_text (GTK_ENTRY (username_entry)));
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
    gtk_widget_show (message_label);
    gtk_label_set_text (GTK_LABEL (message_label), text);
}

static void
authentication_complete_cb (LdmGreeter *greeter)
{
    GtkTreeIter iter;

    gtk_widget_hide (password_entry);
    gtk_entry_set_text (GTK_ENTRY (password_entry), "");

    /* Clear row shading */
    if (user_model && gtk_tree_model_get_iter_first (GTK_TREE_MODEL (user_model), &iter))
    {
        do
        {
            gtk_list_store_set (GTK_LIST_STORE (user_model), &iter, 3, TRUE, -1);
        } while (gtk_tree_model_iter_next (GTK_TREE_MODEL (user_model), &iter));
    }
    if (username_entry)
    {
        gtk_entry_set_text (GTK_ENTRY (username_entry), "");
        gtk_widget_set_sensitive (username_entry, TRUE);
    }

    if (user_view)
        gtk_widget_grab_focus (user_view);
    else
        gtk_widget_grab_focus (username_entry);
  
    if (ldm_greeter_get_is_authenticated (greeter))
    {
        ldm_greeter_login (greeter, ldm_greeter_get_authentication_user (greeter), session);
    }
    else
    {
        gtk_widget_show (message_label);
        gtk_label_set_text (GTK_LABEL (message_label), "Failed to authenticate");
    }
}

static void
timed_login_cb (LdmGreeter *greeter, const gchar *username)
{
    ldm_greeter_login (greeter, ldm_greeter_get_timed_login_user (greeter), ldm_greeter_get_default_session (greeter));
}

static void
suspend_cb (GtkWidget *widget, LdmGreeter *greeter)
{
    ldm_greeter_suspend (greeter);
}

static void
hibernate_cb (GtkWidget *widget, LdmGreeter *greeter)
{
    ldm_greeter_hibernate (greeter);
}

static void
center_window (GtkWindow *window)
{
    GtkAllocation allocation;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;

    gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);
    gtk_window_move (GTK_WINDOW (window),
                     (screen_width - allocation.width) / 2,
                     (screen_height - allocation.height) / 2);
}

static void
restart_cb (GtkWidget *widget, LdmGreeter *greeter)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and restart the computer?"));
    gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), gtk_image_new_from_icon_name ("system-restart", GTK_ICON_SIZE_DIALOG));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Cancel"), FALSE, _("Restart"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        ldm_greeter_restart (greeter);
    gtk_widget_destroy (dialog);
}

static void
shutdown_cb (GtkWidget *widget, LdmGreeter *greeter)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and shutdown the computer?"));
    gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_DIALOG));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Cancel"), FALSE, _("Shutdown"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        ldm_greeter_shutdown (greeter);
    gtk_widget_destroy (dialog);
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
    {
        g_free (session);
        session = g_strdup (g_object_get_data (G_OBJECT (widget), "key"));
    }
}

static void
a11y_font_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        g_object_set (gtk_settings_get_default (), "gtk-font-name", "UbuntuBeta 20", NULL);
    else
        g_object_set (gtk_settings_get_default (), "gtk-font-name", "UbuntuBeta 10", NULL);
}

static void
a11y_contrast_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", "HighContrastInverse", NULL);
    else
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", theme_name, NULL);
}

static void
sigterm_cb (int signum)
{
    exit (0);
}

static GtkWidget *
make_user_view (void)
{
    GtkListStore *model;
    GtkWidget *view;
    GtkCellRenderer *renderer;
    const GList *items, *item;
    GtkTreeIter iter;

    items = ldm_greeter_get_users (greeter);
    if (!items)
        return NULL;
  
    model = gtk_list_store_new (4, G_TYPE_STRING, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_BOOLEAN);
    for (item = items; item; item = item->next)
    {
        LdmUser *user = item->data;
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

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, ldm_user_get_name (user),
                            1, ldm_user_get_display_name (user),
                            2, pixbuf,
                            3, TRUE,
                            -1);
    }

    view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (model));
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view), FALSE);
    gtk_tree_view_set_grid_lines (GTK_TREE_VIEW (view), GTK_TREE_VIEW_GRID_LINES_NONE);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), 0, "Face", renderer, "pixbuf", 2, "sensitive", 3, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (view), 1, "Name", renderer, "text", 1, NULL);

    g_signal_connect (view, "row-activated", G_CALLBACK (user_view_activate_cb), NULL);
    g_signal_connect (view, "button-press-event", G_CALLBACK (user_view_click_cb), NULL);

    if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter))
        gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (view)), &iter);

    return view;
}

int
main(int argc, char **argv)
{
    gchar *theme_dir, *rc_file, *background_image;
    GdkWindow *root;
    const GList *items, *item;
    GSList *session_radio_list = NULL, *language_radio_list = NULL, *layout_radio_list = NULL;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkAllocation allocation;
    GtkWidget *logo_image;
    GtkWidget *option_menu, *power_menu;
    GtkWidget *menu_bar, *menu, *menu_item;
    GdkColor background_color;
    gint n_power_items = 0;

    signal (SIGTERM, sigterm_cb);

    g_type_init ();

    greeter = ldm_greeter_new ();

    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "quit", G_CALLBACK (quit_cb), NULL);

    ldm_greeter_connect (greeter);
    session = g_strdup (ldm_greeter_get_default_session (greeter));

    theme_dir = g_path_get_dirname (ldm_greeter_get_theme (greeter));
    rc_file = ldm_greeter_get_string_property (greeter, "gtkrc");
    if (rc_file)
    {
        gchar *path = g_build_filename (theme_dir, rc_file, NULL);
        g_free (rc_file);
        gtk_rc_add_default_file (path);
        g_free (path);
    }

    gtk_init (&argc, &argv);

    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &theme_name, NULL);

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);
  
    root = gdk_get_default_root_window ();
    gdk_window_set_cursor (root, gdk_cursor_new (GDK_LEFT_PTR));
    //FIXME: background_color = ldm_greeter_get_string_property (greeter, "background-color");
    gdk_color_parse ("#000000", &background_color);
    gdk_color_alloc (gdk_window_get_colormap (root), &background_color);
    gdk_window_set_background (root, &background_color);

    background_image = ldm_greeter_get_string_property (greeter, "background-image");
    if (background_image)
    {
        gchar *path;
        GdkPixbuf *pixbuf;
        GError *error = NULL;

        path = g_build_filename (theme_dir, background_image, NULL);
        g_free (background_image);
        pixbuf = gdk_pixbuf_new_from_file (path, &error);
        if (!pixbuf)
           g_warning ("Failed to load background: %s", error->message);
        g_clear_error (&error);
        g_free (path);

        if (pixbuf)
        {
            GdkPixmap *pixmap;
            GdkPixbuf *scaled;

            scaled = gdk_pixbuf_scale_simple (pixbuf, screen_width, screen_height, GDK_INTERP_BILINEAR);
            g_object_unref (pixbuf);

            gdk_pixbuf_render_pixmap_and_mask_for_colormap (scaled, gdk_window_get_colormap (root), &pixmap, NULL, 0);
            g_object_unref (scaled);

            gdk_window_set_back_pixmap (root, pixmap, FALSE);
        }
        else
            background_image = NULL;
    }
    gdk_window_clear (root);

    user_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (user_window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (user_window), FALSE);
    gtk_container_set_border_width (GTK_CONTAINER (user_window), 12);
    g_signal_connect (user_window, "delete-event", gtk_main_quit, NULL);

    vbox = gtk_vbox_new (FALSE, 6);
    gtk_container_add (GTK_CONTAINER (user_window), vbox);

    logo_image = gtk_image_new_from_icon_name ("computer", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (logo_image), 64);
    gtk_box_pack_start (GTK_BOX (vbox), logo_image, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), gtk_label_new (ldm_greeter_get_hostname (greeter)), FALSE, FALSE, 0);

    message_label = gtk_label_new ("");
    gtk_box_pack_start (GTK_BOX (vbox), message_label, FALSE, FALSE, 0);
    gtk_widget_set_no_show_all (message_label, TRUE);

    user_view = make_user_view ();
    if (user_view)
    {
        username_entry = NULL;
        user_model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));
        gtk_box_pack_start (GTK_BOX (vbox), user_view, FALSE, FALSE, 0);
    }
    else
    {
        username_entry = gtk_entry_new ();
        gtk_box_pack_start (GTK_BOX (vbox), username_entry, FALSE, FALSE, 0);
        g_signal_connect (username_entry, "activate", G_CALLBACK (username_activate_cb), NULL);
        user_model = NULL;
    }

    password_entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY (password_entry), FALSE);
    gtk_widget_set_sensitive (password_entry, FALSE);
    gtk_box_pack_start (GTK_BOX (vbox), password_entry, FALSE, FALSE, 0);
    g_signal_connect (password_entry, "activate", G_CALLBACK (password_activate_cb), NULL);
    gtk_widget_set_no_show_all (password_entry, TRUE);

    gtk_widget_show_all (user_window);

    /* Center the window */
    center_window (GTK_WINDOW (user_window));

    panel_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (panel_window), FALSE);
    gtk_window_set_resizable (GTK_WINDOW (panel_window), FALSE);
    gtk_window_set_default_size (GTK_WINDOW (panel_window), screen_width, 10);

    menu_bar = gtk_menu_bar_new ();
    gtk_container_add (GTK_CONTAINER (panel_window), menu_bar);

    menu_item = gtk_image_menu_item_new ();
    gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), gtk_image_new_from_icon_name ("preferences-desktop-accessibility", GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_menu_item_set_label (GTK_MENU_ITEM (menu_item), ""); // NOTE: Needed to make the icon show as selected
    gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menu_item), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);

    menu_item = gtk_check_menu_item_new_with_label (_("Large Font"));
    g_signal_connect (menu_item, "toggled", G_CALLBACK (a11y_font_cb), NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    menu_item = gtk_check_menu_item_new_with_label (_("High Constrast"));
    g_signal_connect (menu_item, "toggled", G_CALLBACK (a11y_contrast_cb), NULL);
    gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

    menu_item = gtk_menu_item_new_with_label (_("Options"));
    gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
    option_menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), option_menu);

    menu_item = gtk_menu_item_new_with_label (_("Language"));
    gtk_menu_shell_append (GTK_MENU_SHELL (option_menu), menu_item);
    menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
    items = ldm_greeter_get_languages (greeter);
    for (item = items; item; item = item->next)
    {
        LdmLanguage *language = item->data;
        gchar *label;
      
        if (ldm_language_get_name (language)[0] == '\0')
            label = g_strdup (ldm_language_get_code (language));
        else
            label = g_strdup_printf ("%s - %s", ldm_language_get_name (language), ldm_language_get_territory (language));

        menu_item = gtk_radio_menu_item_new_with_label (language_radio_list, label);
        language_radio_list = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (menu_item));
        gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

        if (g_str_equal (ldm_language_get_code (language), ldm_greeter_get_language (greeter)))
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);

        //g_object_set_data (G_OBJECT (menu_item), "language", g_strdup (ldm_language_get_code (language)));
        //g_signal_connect (menu_item, "toggled", G_CALLBACK (language_changed_cb), NULL);
    }

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

        if (g_str_equal (ldm_session_get_key (session), ldm_greeter_get_default_session (greeter)))
            gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);

        g_object_set_data (G_OBJECT (menu_item), "key", g_strdup (ldm_session_get_key (session)));
        g_signal_connect (menu_item, "toggled", G_CALLBACK (session_changed_cb), NULL);
    }

    power_menu = gtk_menu_new ();
    if (ldm_greeter_get_can_suspend (greeter))
    {
        menu_item = gtk_menu_item_new_with_label (_("Suspend"));
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), menu_item);
        g_signal_connect (menu_item, "activate", G_CALLBACK (suspend_cb), greeter);
        n_power_items++;
    }
    if (ldm_greeter_get_can_hibernate (greeter))
    {
        menu_item = gtk_menu_item_new_with_label (_("Hibernate"));
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), menu_item);
        g_signal_connect (menu_item, "activate", G_CALLBACK (hibernate_cb), greeter);
        n_power_items++;
    }
    if (ldm_greeter_get_can_restart (greeter))
    {
        menu_item = gtk_menu_item_new_with_label (_("Restart..."));
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), menu_item);
        g_signal_connect (menu_item, "activate", G_CALLBACK (restart_cb), greeter);
        n_power_items++;
    }
    if (ldm_greeter_get_can_shutdown (greeter))
    {
        menu_item = gtk_menu_item_new_with_label (_("Shutdown..."));
        gtk_menu_shell_append (GTK_MENU_SHELL (power_menu), menu_item);
        g_signal_connect (menu_item, "activate", G_CALLBACK (shutdown_cb), greeter);
        n_power_items++;
    }
    if (n_power_items > 0)
    {
        menu_item = gtk_image_menu_item_new ();
        gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (menu_item), TRUE);
        gtk_menu_item_set_right_justified (GTK_MENU_ITEM (menu_item), TRUE);
        gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_LARGE_TOOLBAR));
        gtk_menu_item_set_label (GTK_MENU_ITEM (menu_item), ""); // NOTE: Needed to make the icon show as selected
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), power_menu);
        gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
    }

    gtk_widget_show_all (panel_window);

    gtk_widget_get_allocation (panel_window, &allocation);
    gtk_widget_set_size_request (GTK_WIDGET (panel_window), screen_width, allocation.height);  
    gtk_window_move (GTK_WINDOW (panel_window), 0, screen_height - allocation.height);

    gtk_widget_grab_focus (user_view);

    gtk_main ();

    return 0;
}
