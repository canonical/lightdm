/*
 * Copyright (C) 2010-2011 Robert Ancell.
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
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>

#include "lightdm/greeter.h"

static LdmGreeter *greeter;
static GtkWidget *window, *message_label, *user_view;
static GdkPixbuf *background_pixbuf;
static GtkWidget *prompt_box, *prompt_label, *prompt_entry, *session_combo;
static gchar *theme_name;

static gchar *
get_session ()
{
    GtkTreeIter iter;
    gchar *session;

    if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (session_combo), &iter))
        return g_strdup (ldm_greeter_get_default_session (greeter));

    gtk_tree_model_get (gtk_combo_box_get_model (GTK_COMBO_BOX (session_combo)), &iter, 1, &session, -1);

    return session;
}

static void
set_session (const gchar *session)
{
    GtkTreeModel *model = gtk_combo_box_get_model (GTK_COMBO_BOX (session_combo));
    GtkTreeIter iter;

    if (!gtk_tree_model_get_iter_first (model, &iter))
        return;
  
    do
    {
        gchar *s;
        gboolean matched;
        gtk_tree_model_get (model, &iter, 1, &s, -1);
        matched = strcmp (s, session) == 0;
        g_free (s);
        if (matched)
        {
            gtk_combo_box_set_active_iter (GTK_COMBO_BOX (session_combo), &iter);
            return;
        }
    } while (gtk_tree_model_iter_next (model, &iter));
}

static void
start_authentication (const gchar *username)
{
    gchar *language, *layout, *session;

    gtk_widget_hide (message_label);
    gtk_label_set_text (GTK_LABEL (message_label), "");

    if (strcmp (username, "*other") == 0)
    {
        ldm_greeter_login_with_user_prompt (greeter);
    }
    else if (strcmp (username, "*guest") == 0)
    {
        ldm_greeter_login_as_guest (greeter);
    }
    else
    {
        if (ldm_greeter_get_user_defaults (greeter, username, &language, &layout, &session))
        {
            set_session (session);
            g_free (language);
            g_free (layout);
            g_free (session);
        }

        ldm_greeter_login (greeter, username);
    }
}

void user_treeview_row_activated_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column);
G_MODULE_EXPORT
void
user_treeview_row_activated_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column)
{
    GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));  
    GtkTreeIter iter;
    gchar *user;

    gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, path);
    gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);
    start_authentication (user);
    g_free (user);
}

static gboolean
idle_select_cb ()
{
    GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));
    GtkTreeIter iter;
    gchar *user;

    if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (GTK_TREE_VIEW (user_view)),
                                         NULL, &iter))
    {
        gtk_tree_model_get (GTK_TREE_MODEL (model), &iter, 0, &user, -1);
        start_authentication (user);
        g_free (user);
    }

    return FALSE;
}

gboolean user_treeview_button_press_event_cb (GtkWidget *widget, GdkEventButton *event);
G_MODULE_EXPORT
gboolean
user_treeview_button_press_event_cb (GtkWidget *widget, GdkEventButton *event)
{
    /* Do it in the idle loop so the selection is done first */
    g_idle_add (idle_select_cb, NULL);
    return FALSE;
}

void login_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
login_cb (GtkWidget *widget)
{
    gtk_widget_set_sensitive (prompt_entry, FALSE);
    if (!ldm_greeter_get_in_authentication (greeter))
        start_authentication (gtk_entry_get_text (GTK_ENTRY (prompt_entry)));
    else
        ldm_greeter_respond (greeter, gtk_entry_get_text (GTK_ENTRY (prompt_entry)));
    gtk_entry_set_text (GTK_ENTRY (prompt_entry), "");
}

void cancel_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
cancel_cb (GtkWidget *widget)
{
    ldm_greeter_cancel_authentication (greeter);
}

static void
show_prompt_cb (LdmGreeter *greeter, const gchar *text)
{
    gtk_label_set_text (GTK_LABEL (prompt_label), text);
    gtk_widget_set_sensitive (prompt_entry, TRUE);
    gtk_entry_set_text (GTK_ENTRY (prompt_entry), "");
    gtk_entry_set_visibility (GTK_ENTRY (prompt_entry), FALSE);
    gtk_widget_show (prompt_box);
    gtk_widget_grab_focus (prompt_entry);
}

static void
show_message_cb (LdmGreeter *greeter, const gchar *text)
{
    gtk_label_set_text (GTK_LABEL (message_label), text);
    gtk_widget_show (message_label);
}

static void
authentication_complete_cb (LdmGreeter *greeter)
{
    gtk_widget_hide (prompt_box);
    gtk_label_set_text (GTK_LABEL (prompt_label), "");
    gtk_entry_set_text (GTK_ENTRY (prompt_entry), "");

    gtk_widget_grab_focus (user_view);

    if (ldm_greeter_get_is_authenticated (greeter))
    {
        gchar *session = get_session ();
        ldm_greeter_start_session (greeter, session);
        g_free (session);
    }
    else
    {
        gtk_label_set_text (GTK_LABEL (message_label), "Failed to authenticate");
        gtk_widget_show (message_label);
    }
}

static void
timed_login_cb (LdmGreeter *greeter, const gchar *username)
{
    set_session (ldm_greeter_get_default_session (greeter));
    ldm_greeter_login (greeter, ldm_greeter_get_timed_login_user (greeter));
}

void suspend_cb (GtkWidget *widget, LdmGreeter *greeter);
G_MODULE_EXPORT
void
suspend_cb (GtkWidget *widget, LdmGreeter *greeter)
{
    ldm_greeter_suspend (greeter);
}

void hibernate_cb (GtkWidget *widget, LdmGreeter *greeter);
G_MODULE_EXPORT
void
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

void restart_cb (GtkWidget *widget, LdmGreeter *greeter);
G_MODULE_EXPORT
void
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

void shutdown_cb (GtkWidget *widget, LdmGreeter *greeter);
G_MODULE_EXPORT
void
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

static gboolean
fade_timer_cb (gpointer data)
{
    gdouble opacity;

    opacity = gtk_window_get_opacity (GTK_WINDOW (window));
    opacity -= 0.1;
    if (opacity <= 0)
    {
        gtk_main_quit ();
        return FALSE;
    }
    gtk_window_set_opacity (GTK_WINDOW (window), opacity);

    return TRUE;
}

static void
user_added_cb (LdmGreeter *greeter, LdmUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, ldm_user_get_name (user),
                        1, ldm_user_get_display_name (user),
                        /*2, pixbuf,*/
                        -1);
}

static gboolean
get_user_iter (const gchar *username, GtkTreeIter *iter)
{
    GtkTreeModel *model;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));
  
    if (!gtk_tree_model_get_iter_first (model, iter))
        return FALSE;
    do
    {
        gchar *name;
        gboolean matched;

        gtk_tree_model_get (model, iter, 0, &name, -1);
        matched = strcmp (name, username) == 0;
        g_free (name);
        if (matched)
            return TRUE;
    } while (gtk_tree_model_iter_next (model, iter));

    return FALSE;
}

static void
user_changed_cb (LdmGreeter *greeter, LdmUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (ldm_user_get_name (user), &iter))
        return;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, ldm_user_get_name (user),
                        1, ldm_user_get_display_name (user),
                        /*2, pixbuf,*/
                        -1);
}

static void
user_removed_cb (LdmGreeter *greeter, LdmUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (ldm_user_get_name (user), &iter))
        return;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));  
    gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

static void
quit_cb (LdmGreeter *greeter, const gchar *username)
{
    /* Fade out the greeter */
    g_timeout_add (40, (GSourceFunc) fade_timer_cb, NULL);
}

void a11y_font_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_font_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        g_object_set (gtk_settings_get_default (), "gtk-font-name", "Ubuntu 20", NULL);
    else
        g_object_set (gtk_settings_get_default (), "gtk-font-name", "Ubuntu 10", NULL);
}

void a11y_contrast_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
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

gboolean draw_background_cb (GtkWidget *widget, GdkEventExpose *event);
G_MODULE_EXPORT
gboolean
draw_background_cb (GtkWidget *widget, GdkEventExpose *event)
{
    cairo_t *context;
    GtkAllocation allocation;

    context = gdk_cairo_create (GDK_DRAWABLE (gtk_widget_get_window (widget)));

    gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
    gdk_cairo_set_source_pixbuf (context, background_pixbuf, 0.0, 0.0);
    gdk_cairo_region (context, event->region);
    cairo_fill (context);

    cairo_destroy (context);

    return FALSE;
}

static void
connect_cb (LdmGreeter *greeter)
{
    GdkWindow *root;
    GdkDisplay *display;
    GdkScreen *screen;
    gint screen_width, screen_height;
    GtkBuilder *builder;
    const GList *items, *item;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    gchar *theme_dir, *rc_file, *background_image;
    GError *error = NULL;

    display = gdk_display_get_default ();
    screen = gdk_display_get_default_screen (display);
    screen_width = gdk_screen_get_width (screen);
    screen_height = gdk_screen_get_height (screen);

    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &theme_name, NULL);
    theme_dir = g_path_get_dirname (ldm_greeter_get_theme (greeter));
    rc_file = ldm_greeter_get_string_property (greeter, "gtkrc");
    if (rc_file)
    {
        gchar *path = g_build_filename (theme_dir, rc_file, NULL);
        g_free (rc_file);
        gtk_rc_add_default_file (path);
        g_free (path);
    }

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_file (builder, UI_DIR "/greeter.ui", &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        return;
    }
    g_clear_error (&error);
    window = GTK_WIDGET (gtk_builder_get_object (builder, "greeter_window"));
    prompt_box = GTK_WIDGET (gtk_builder_get_object (builder, "prompt_box"));
    prompt_label = GTK_WIDGET (gtk_builder_get_object (builder, "prompt_label"));
    prompt_entry = GTK_WIDGET (gtk_builder_get_object (builder, "prompt_entry"));
    message_label = GTK_WIDGET (gtk_builder_get_object (builder, "message_label"));
    session_combo = GTK_WIDGET (gtk_builder_get_object (builder, "session_combobox"));
  
    gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "hostname_label")), ldm_greeter_get_hostname (greeter));

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
            background_pixbuf = gdk_pixbuf_scale_simple (pixbuf, screen_width, screen_height, GDK_INTERP_BILINEAR);
            g_object_unref (pixbuf);
        }
    }

    /* Set the background */
    root = gdk_get_default_root_window ();
    gdk_window_set_cursor (root, gdk_cursor_new (GDK_LEFT_PTR));
    if (background_pixbuf)
    {
        GdkPixmap *pixmap;

        gdk_pixbuf_render_pixmap_and_mask_for_colormap (background_pixbuf, gdk_window_get_colormap (root), &pixmap, NULL, 0);
        gdk_window_set_back_pixmap (root, pixmap, FALSE);
    }

    if (!ldm_greeter_get_can_suspend (greeter))
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
    if (!ldm_greeter_get_can_hibernate (greeter))
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
    if (!ldm_greeter_get_can_restart (greeter))
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
    if (!ldm_greeter_get_can_shutdown (greeter))
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

    user_view = GTK_WIDGET (gtk_builder_get_object (builder, "user_treeview"));
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 0, "Face", gtk_cell_renderer_pixbuf_new(), "pixbuf", 2, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (user_view), 1, "Name", gtk_cell_renderer_text_new(), "text", 1, NULL);

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (user_view));
    items = ldm_greeter_get_users (greeter);
    for (item = items; item; item = item->next)
    {
        LdmUser *user = item->data;
        const gchar *image;
        GdkPixbuf *pixbuf = NULL;

        image = ldm_user_get_image (user);
        if (image)
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
                                               GTK_ICON_LOOKUP_USE_BUILTIN,
                                               NULL);
        /*if (!pixbuf)
        {
            pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 64, 64);
            memset (gdk_pixbuf_get_pixels (pixbuf), 0, gdk_pixbuf_get_height (pixbuf) * gdk_pixbuf_get_rowstride (pixbuf) * gdk_pixbuf_get_n_channels (pixbuf));
        }*/

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, ldm_user_get_name (user),
                            1, ldm_user_get_display_name (user),
                            2, pixbuf,
                            -1);
    }
    if (ldm_greeter_get_has_guest_session (greeter))
    {
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, "*guest",
                            1, "Guest Account",
                            2, gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "stock_person", 64, 0, NULL),
                            -1);
    }

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, "*other",
                        1, "Other...",
                        2, gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "stock_person", 64, 0, NULL),
                        -1);

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (session_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (session_combo), renderer, "text", 0);
    model = gtk_combo_box_get_model (GTK_COMBO_BOX (session_combo));
    items = ldm_greeter_get_sessions (greeter);
    for (item = items; item; item = item->next)
    {
        LdmSession *session = item->data;

        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, ldm_session_get_name (session),
                            1, ldm_session_get_key (session),
                            -1);
    }
    set_session (ldm_greeter_get_default_session (greeter));

    gtk_window_set_default_size (GTK_WINDOW (window), screen_width, screen_height);
    gtk_builder_connect_signals(builder, greeter);
    gtk_widget_show (window);

    gtk_widget_grab_focus (user_view);
}

int
main(int argc, char **argv)
{
    /* Disable global menus */
    unsetenv ("UBUNTU_MENUPROXY");

    signal (SIGTERM, sigterm_cb);
  
    g_type_init ();

    greeter = ldm_greeter_new ();
    g_signal_connect (G_OBJECT (greeter), "connected", G_CALLBACK (connect_cb), NULL);    
    g_signal_connect (G_OBJECT (greeter), "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (G_OBJECT (greeter), "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "show-error", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "timed-login", G_CALLBACK (timed_login_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "user-added", G_CALLBACK (user_added_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "user-changed", G_CALLBACK (user_changed_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "user-removed", G_CALLBACK (user_removed_cb), NULL);
    g_signal_connect (G_OBJECT (greeter), "quit", G_CALLBACK (quit_cb), NULL);
    ldm_greeter_connect_to_server (greeter);

    gtk_init (&argc, &argv);

    gtk_main ();

    return 0;
}
