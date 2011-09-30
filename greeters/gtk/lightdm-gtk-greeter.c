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
#include <cairo-xlib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdkx.h>

#include "lightdm.h"

static LightDMGreeter *greeter;
static GtkWindow *login_window, *panel_window;
static GtkLabel *message_label, *prompt_label;
static GtkTreeView *user_view;
static GtkWidget *login_box, *prompt_box;
static GtkEntry *prompt_entry;
static GtkComboBox *session_combo;
static GtkComboBox *language_combo;
static gchar *default_font_name, *default_theme_name;
static gboolean cancelling = FALSE, prompted = FALSE;

static gchar *
get_session ()
{
    GtkTreeIter iter;
    gchar *session;

    if (!gtk_combo_box_get_active_iter (session_combo, &iter))
        return g_strdup (lightdm_greeter_get_default_session_hint (greeter));

    gtk_tree_model_get (gtk_combo_box_get_model (session_combo), &iter, 1, &session, -1);

    return session;
}

static void
set_session (const gchar *session)
{
    GtkTreeModel *model = gtk_combo_box_get_model (session_combo);
    GtkTreeIter iter;
    const gchar *default_session;

    if (session && gtk_tree_model_get_iter_first (model, &iter))
    {
        do
        {
            gchar *s;
            gboolean matched;
            gtk_tree_model_get (model, &iter, 1, &s, -1);
            matched = strcmp (s, session) == 0;
            g_free (s);
            if (matched)
            {
                gtk_combo_box_set_active_iter (session_combo, &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    /* If failed to find this session, then try the default */
    default_session = lightdm_greeter_get_default_session_hint (greeter);
    if (default_session && g_strcmp0 (session, default_session) != 0)
    {
        set_session (lightdm_greeter_get_default_session_hint (greeter));
        return;
    }

    /* Otherwise just pick the first session */
    gtk_combo_box_set_active (session_combo, 0);
}

static gchar *
get_language ()
{
    GtkTreeIter iter;
    gchar *language;

    if (!gtk_combo_box_get_active_iter (language_combo, &iter))
        return NULL;

    gtk_tree_model_get (gtk_combo_box_get_model (language_combo), &iter, 1, &language, -1);

    return language;
}

static void
set_language (const gchar *language)
{
    GtkTreeModel *model = gtk_combo_box_get_model (language_combo);
    GtkTreeIter iter;
    const gchar *default_language = NULL;

    if (language && gtk_tree_model_get_iter_first (model, &iter))
    {
        do
        {
            gchar *s;
            gboolean matched;
            gtk_tree_model_get (model, &iter, 1, &s, -1);
            matched = strcmp (s, language) == 0;
            g_free (s);
            if (matched)
            {
                gtk_combo_box_set_active_iter (language_combo, &iter);
                return;
            }
        } while (gtk_tree_model_iter_next (model, &iter));
    }

    /* If failed to find this language, then try the default */
    if (lightdm_get_language ())
        default_language = lightdm_language_get_code (lightdm_get_language ());
    if (default_language && g_strcmp0 (default_language, language) != 0)
        set_language (default_language);
}

static void
set_message_label (const gchar *text)
{
    gtk_widget_set_visible (GTK_WIDGET (message_label), strcmp (text, "") != 0);
    gtk_label_set_text (message_label, text);
}

static void
start_authentication (const gchar *username)
{
    cancelling = FALSE;
    prompted = FALSE;

    if (!username)
    {
        lightdm_greeter_authenticate (greeter, NULL);
    }
    else if (strcmp (username, "*guest") == 0)
    {
        lightdm_greeter_authenticate_as_guest (greeter);
    }
    else
    {
        LightDMUser *user;

        user = lightdm_user_list_get_user_by_name (lightdm_user_list_get_instance (), username);
        if (user)
        {
            set_session (lightdm_user_get_session (user));
            set_language (lightdm_user_get_language (user));
        }
        else
        {
            set_session (NULL);
            set_language (NULL);
        }

        lightdm_greeter_authenticate (greeter, username);
    }
}

static void
cancel_authentication (void)
{
    /* If in authentication then stop that first */
    cancelling = FALSE;
    if (lightdm_greeter_get_in_authentication (greeter))
    {
        cancelling = TRUE;
        lightdm_greeter_cancel_authentication (greeter);
        return;
    }

    /* Start a new login or return to the user list */
    if (lightdm_greeter_get_hide_users_hint (greeter))
        start_authentication (NULL);
    else
    {
        gtk_widget_hide (login_box);
        gtk_widget_grab_focus (GTK_WIDGET (user_view));
    }
}

static void
start_session (void)
{
    gchar *language;
    gchar *session;

    language = get_language ();
    if (language)
        lightdm_greeter_set_language (greeter, language);
    g_free (language);

    session = get_session ();
    if (!lightdm_greeter_start_session_sync (greeter, session, NULL))
    {
        set_message_label (_("Failed to start session"));
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
    }
    g_free (session);
}

void user_treeview_row_activated_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column);
G_MODULE_EXPORT
void
user_treeview_row_activated_cb (GtkWidget *widget, GtkTreePath *path, GtkTreeViewColumn *column)
{
    GtkTreeModel *model = gtk_tree_view_get_model (user_view);  
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
    GtkTreeModel *model = gtk_tree_view_get_model (user_view);
    GtkTreeIter iter;
    gchar *user;

    if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection (user_view),
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
    gtk_widget_set_sensitive (GTK_WIDGET (prompt_entry), FALSE);
    set_message_label ("");

    if (lightdm_greeter_get_is_authenticated (greeter))
        start_session ();
    else if (lightdm_greeter_get_in_authentication (greeter))
        lightdm_greeter_respond (greeter, gtk_entry_get_text (prompt_entry));
    else
        start_authentication (lightdm_greeter_get_authentication_user (greeter));
}

void cancel_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
cancel_cb (GtkWidget *widget)
{
    cancel_authentication ();
}

static void
show_prompt_cb (LightDMGreeter *greeter, const gchar *text, LightDMPromptType type)
{
    prompted = TRUE;

    gtk_widget_show (GTK_WIDGET (login_box));
    gtk_label_set_text (prompt_label, text);
    gtk_widget_set_sensitive (GTK_WIDGET (prompt_entry), TRUE);
    gtk_entry_set_text (prompt_entry, "");
    gtk_entry_set_visibility (prompt_entry, type != LIGHTDM_PROMPT_TYPE_SECRET);
    gtk_widget_show (GTK_WIDGET (prompt_box));
    gtk_widget_grab_focus (GTK_WIDGET (prompt_entry));
}

static void
show_message_cb (LightDMGreeter *greeter, const gchar *text, LightDMMessageType type)
{
    set_message_label (text);
}

static void
authentication_complete_cb (LightDMGreeter *greeter)
{
    gtk_entry_set_text (prompt_entry, "");

    if (cancelling)
    {
        cancel_authentication ();
        return;
    }

    gtk_widget_hide (prompt_box);
    gtk_widget_show (login_box);

    if (lightdm_greeter_get_is_authenticated (greeter))
    {
        if (prompted)
            start_session ();
    }
    else
    {
        if (prompted)
        {
            set_message_label (_("Incorrect password, please try again"));
            start_authentication (lightdm_greeter_get_authentication_user (greeter));
        }
        else
            set_message_label (_("Failed to authenticate"));
    }
}

static void
autologin_timer_expired_cb (LightDMGreeter *greeter)
{
    if (lightdm_greeter_get_autologin_guest_hint (greeter))
        start_authentication ("*guest");
    else if (lightdm_greeter_get_autologin_user_hint (greeter))
        start_authentication (lightdm_greeter_get_autologin_user_hint (greeter));
}

static void
center_window (GtkWindow *window)
{
    GdkScreen *screen;
    GtkAllocation allocation;
    GdkRectangle monitor_geometry;

    screen = gtk_window_get_screen (window);
    gdk_screen_get_monitor_geometry (screen, gdk_screen_get_primary_monitor (screen), &monitor_geometry);
    gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
    gtk_window_move (window,
                     monitor_geometry.x + (monitor_geometry.width - allocation.width) / 2,
                     monitor_geometry.y + (monitor_geometry.height - allocation.height) / 2);
}

void login_window_size_allocate_cb (GtkWidget *widget, GdkRectangle *allocation);
G_MODULE_EXPORT
void
login_window_size_allocate_cb (GtkWidget *widget, GdkRectangle *allocation)
{
    center_window (GTK_WINDOW (widget));
}

void suspend_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
suspend_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    lightdm_suspend (NULL);
}

void hibernate_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
hibernate_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    lightdm_hibernate (NULL);
}

void restart_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
restart_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    GtkWidget *dialog;

    gtk_widget_hide (GTK_WIDGET (login_window));

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and restart the computer?"));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Return To Login"), FALSE, _("Restart"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        lightdm_restart (NULL);

    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));
}

void shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter);
G_MODULE_EXPORT
void
shutdown_cb (GtkWidget *widget, LightDMGreeter *greeter)
{
    GtkWidget *dialog;

    gtk_widget_hide (GTK_WIDGET (login_window));

    dialog = gtk_message_dialog_new (NULL,
                                     GTK_DIALOG_MODAL,
                                     GTK_MESSAGE_OTHER,
                                     GTK_BUTTONS_NONE,
                                     "%s", _("Are you sure you want to close all programs and shutdown the computer?"));
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Return To Login"), FALSE, _("Shutdown"), TRUE, NULL);
    gtk_widget_show_all (dialog);
    center_window (GTK_WINDOW (dialog));

    if (gtk_dialog_run (GTK_DIALOG (dialog)))
        lightdm_shutdown (NULL);

    gtk_widget_destroy (dialog);
    gtk_widget_show (GTK_WIDGET (login_window));
}

static void
user_added_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_tree_view_get_model (user_view);

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                        /*3, pixbuf,*/
                        -1);
}

static gboolean
get_user_iter (const gchar *username, GtkTreeIter *iter)
{
    GtkTreeModel *model;

    model = gtk_tree_view_get_model (user_view);
  
    if (!gtk_tree_model_get_iter_first (model, iter))
        return FALSE;
    do
    {
        gchar *name;
        gboolean matched;

        gtk_tree_model_get (model, iter, 0, &name, -1);
        matched = g_strcmp0 (name, username) == 0;
        g_free (name);
        if (matched)
            return TRUE;
    } while (gtk_tree_model_iter_next (model, iter));

    return FALSE;
}

static void
user_changed_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (lightdm_user_get_name (user), &iter))
        return;

    model = gtk_tree_view_get_model (user_view);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, lightdm_user_get_name (user),
                        1, lightdm_user_get_display_name (user),
                        2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                        /*3, pixbuf,*/
                        -1);
}

static void
user_removed_cb (LightDMUserList *user_list, LightDMUser *user)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!get_user_iter (lightdm_user_get_name (user), &iter))
        return;

    model = gtk_tree_view_get_model (user_view);  
    gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
}

void a11y_font_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_font_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
    {
        gchar *font_name, **tokens;

        g_object_get (gtk_settings_get_default (), "gtk-font-name", &font_name, NULL);
        tokens = g_strsplit (font_name, " ", 2);
        if (g_strv_length (tokens) == 2)
        {
            gint size = atoi (tokens[1]);
            if (size > 0)
            {
                g_free (font_name);
                font_name = g_strdup_printf ("%s %d", tokens[0], size + 10);
            }
        }
        g_strfreev (tokens);

        g_object_set (gtk_settings_get_default (), "gtk-font-name", font_name, NULL);
    }
    else
        g_object_set (gtk_settings_get_default (), "gtk-font-name", default_font_name, NULL);
}

void a11y_contrast_cb (GtkWidget *widget);
G_MODULE_EXPORT
void
a11y_contrast_cb (GtkWidget *widget)
{
    if (gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM (widget)))
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", "HighContrastInverse", NULL);
    else
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", default_theme_name, NULL);
}

static void
sigterm_cb (int signum)
{
    exit (0);
}

static void
load_user_list ()
{
    const GList *items, *item;
    GtkTreeModel *model;
    GtkTreeIter iter;

    g_signal_connect (lightdm_user_list_get_instance (), "user-added", G_CALLBACK (user_added_cb), NULL);
    g_signal_connect (lightdm_user_list_get_instance (), "user-changed", G_CALLBACK (user_changed_cb), NULL);
    g_signal_connect (lightdm_user_list_get_instance (), "user-removed", G_CALLBACK (user_removed_cb), NULL);

    model = gtk_tree_view_get_model (user_view);
    items = lightdm_user_list_get_users (lightdm_user_list_get_instance ());
    for (item = items; item; item = item->next)
    {
        LightDMUser *user = item->data;
        const gchar *image;
        GdkPixbuf *pixbuf = NULL;

        image = lightdm_user_get_image (user);
        if (image)
            pixbuf = gdk_pixbuf_new_from_file_at_scale (image, 64, 64, TRUE, NULL);
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
                            0, lightdm_user_get_name (user),
                            1, lightdm_user_get_display_name (user),
                            2, lightdm_user_get_logged_in (user) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                            3, pixbuf,
                            -1);

        if (lightdm_greeter_get_select_user_hint (greeter) &&
            strcmp (lightdm_greeter_get_select_user_hint (greeter), lightdm_user_get_name (user)) == 0)
            gtk_tree_selection_select_iter (gtk_tree_view_get_selection (user_view), &iter);
    }
    if (lightdm_greeter_get_has_guest_account_hint (greeter))
    {
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, "*guest",
                            1, "Guest Account",
                            2, PANGO_WEIGHT_NORMAL,
                            3, gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "stock_person", 64, 0, NULL),
                            -1);
        if (lightdm_greeter_get_select_guest_hint (greeter))
            gtk_tree_selection_select_iter (gtk_tree_view_get_selection (user_view), &iter);
    }

    gtk_list_store_append (GTK_LIST_STORE (model), &iter);
    gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                        0, NULL,
                        1, "Other...",
                        2, PANGO_WEIGHT_NORMAL,
                        3, gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "stock_person", 64, 0, NULL),
                        -1);
}

static cairo_surface_t *
create_root_surface (GdkScreen *screen)
{
    gint number, width, height;
    Display *display;
    Pixmap pixmap;
    cairo_surface_t *surface;

    number = gdk_screen_get_number (screen);
    width = gdk_screen_get_width (screen);
    height = gdk_screen_get_height (screen);

    /* Open a new connection so with Retain Permanent so the pixmap remains when the greeter quits */
    gdk_flush ();
    display = XOpenDisplay (gdk_display_get_name (gdk_screen_get_display (screen)));
    if (!display)
    {
        g_warning ("Failed to create root pixmap");
        return NULL;
    }
    XSetCloseDownMode (display, RetainPermanent);
    pixmap = XCreatePixmap (display, RootWindow (display, number), width, height, DefaultDepth (display, number));
    XCloseDisplay (display);

    /* Convert into a Cairo surface */
    surface = cairo_xlib_surface_create (GDK_SCREEN_XDISPLAY (screen),
                                         pixmap,
                                         GDK_VISUAL_XVISUAL (gdk_screen_get_system_visual (screen)),
                                         width, height);

    /* Use this pixmap for the background */
    XSetWindowBackgroundPixmap (GDK_SCREEN_XDISPLAY (screen),
                                RootWindow (GDK_SCREEN_XDISPLAY (screen), number),
                                cairo_xlib_surface_get_drawable (surface));


    return surface;  
}

int
main (int argc, char **argv)
{
    GKeyFile *config;
    GdkRectangle monitor_geometry;
    GtkBuilder *builder;
    GtkTreeModel *model;
    const GList *items, *item;
    GtkTreeIter iter;
    GtkCellRenderer *renderer;
    GtkWidget *menuitem, *hbox, *image;
    gchar *value;
    GdkPixbuf *background_pixbuf = NULL;
    GdkColor background_color;
    gint i;
    GError *error = NULL;

    /* Disable global menus */
    g_unsetenv ("UBUNTU_MENUPROXY");

    /* Initialize i18n */
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    signal (SIGTERM, sigterm_cb);

    config = g_key_file_new ();
    if (!g_key_file_load_from_file (config, CONFIG_FILE, G_KEY_FILE_NONE, &error) &&
        !g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("Failed to load configuration from %s: %s\n", CONFIG_FILE, error->message);
    g_clear_error (&error);

    gtk_init (&argc, &argv);

    greeter = lightdm_greeter_new ();
    g_signal_connect (greeter, "show-prompt", G_CALLBACK (show_prompt_cb), NULL);  
    g_signal_connect (greeter, "show-message", G_CALLBACK (show_message_cb), NULL);
    g_signal_connect (greeter, "authentication-complete", G_CALLBACK (authentication_complete_cb), NULL);
    g_signal_connect (greeter, "autologin-timer-expired", G_CALLBACK (autologin_timer_expired_cb), NULL);
    if (!lightdm_greeter_connect_sync (greeter, NULL))
        return EXIT_FAILURE;

    /* Set default cursor */
    gdk_window_set_cursor (gdk_get_default_root_window (), gdk_cursor_new (GDK_LEFT_PTR));

    /* Load background */
    value = g_key_file_get_value (config, "greeter", "background", NULL);
    if (!value)
        value = g_strdup ("#000000");
    if (!gdk_color_parse (value, &background_color))
    {
        gchar *path;
        GError *error = NULL;

        if (g_path_is_absolute (value))
            path = g_strdup (value);
        else
            path = g_build_filename (GREETER_DATA_DIR, value, NULL);

        g_debug ("Loading background %s", path);
        background_pixbuf = gdk_pixbuf_new_from_file (path, &error);
        if (!background_pixbuf)
           g_warning ("Failed to load background: %s", error->message);
        g_clear_error (&error);
        g_free (path);
    }
    else
        g_debug ("Using background color %s", value);
    g_free (value);

    /* Set the background */
    for (i = 0; i < gdk_display_get_n_screens (gdk_display_get_default ()); i++)
    {
        GdkScreen *screen;
        cairo_surface_t *surface;
        cairo_t *c;
        int monitor;

        screen = gdk_display_get_screen (gdk_display_get_default (), i);
        surface = create_root_surface (screen);
        c = cairo_create (surface);

        for (monitor = 0; monitor < gdk_screen_get_n_monitors (screen); monitor++)
        {
            gdk_screen_get_monitor_geometry (screen, monitor, &monitor_geometry);

            if (background_pixbuf)
            {
                GdkPixbuf *pixbuf = gdk_pixbuf_scale_simple (background_pixbuf, monitor_geometry.width, monitor_geometry.height, GDK_INTERP_BILINEAR);
                gdk_cairo_set_source_pixbuf (c, pixbuf, monitor_geometry.x, monitor_geometry.y);
                g_object_unref (pixbuf);
            }
            else
                gdk_cairo_set_source_color (c, &background_color);
            cairo_paint (c);
        }

        cairo_destroy (c);

        /* Refresh background */
        gdk_flush ();
        XClearWindow (GDK_SCREEN_XDISPLAY (screen), RootWindow (GDK_SCREEN_XDISPLAY (screen), i));
    }
    if (background_pixbuf)
        g_object_unref (background_pixbuf);

    /* Set GTK+ settings */
    value = g_key_file_get_value (config, "greeter", "theme-name", NULL);
    if (value)
    {
        g_debug ("Using theme %s", value);
        g_object_set (gtk_settings_get_default (), "gtk-theme-name", value, NULL);
    }
    g_free (value);
    g_object_get (gtk_settings_get_default (), "gtk-theme-name", &default_theme_name, NULL);
    g_debug ("Default theme is '%s'", default_theme_name);

    value = g_key_file_get_value (config, "greeter", "font-name", NULL);
    if (value)
    {
        g_debug ("Using font %s", value);
        g_object_set (gtk_settings_get_default (), "gtk-font-name", value, NULL);
    }
    g_object_get (gtk_settings_get_default (), "gtk-font-name", &default_font_name, NULL);  
    value = g_key_file_get_value (config, "greeter", "xft-dpi", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-dpi", (int) (1024 * atof (value)), NULL);
    value = g_key_file_get_value (config, "greeter", "xft-antialias", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-antialias", strcmp (value, "true") == 0, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-hintstyle", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-hintstyle", value, NULL);
    g_free (value);
    value = g_key_file_get_value (config, "greeter", "xft-rgba", NULL);
    if (value)
        g_object_set (gtk_settings_get_default (), "gtk-xft-rgba", value, NULL);
    g_free (value);

    /* Load out installed icons */
    gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (), GREETER_DATA_DIR);
    gchar **path;
    gtk_icon_theme_get_search_path (gtk_icon_theme_get_default (), &path, NULL);

    builder = gtk_builder_new ();
    if (!gtk_builder_add_from_file (builder, GREETER_DATA_DIR "/greeter.ui", &error))
    {
        g_warning ("Error loading UI: %s", error->message);
        return EXIT_FAILURE;
    }
    g_clear_error (&error);

    login_window = GTK_WINDOW (gtk_builder_get_object (builder, "login_window"));
    login_box = GTK_WIDGET (gtk_builder_get_object (builder, "login_box"));
    prompt_box = GTK_WIDGET (gtk_builder_get_object (builder, "prompt_box"));
    prompt_label = GTK_LABEL (gtk_builder_get_object (builder, "prompt_label"));
    prompt_entry = GTK_ENTRY (gtk_builder_get_object (builder, "prompt_entry"));
    message_label = GTK_LABEL (gtk_builder_get_object (builder, "message_label"));
    session_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "session_combobox"));
    language_combo = GTK_COMBO_BOX (gtk_builder_get_object (builder, "language_combobox"));  
    panel_window = GTK_WINDOW (gtk_builder_get_object (builder, "panel_window"));

    gtk_label_set_text (GTK_LABEL (gtk_builder_get_object (builder, "hostname_label")), lightdm_get_hostname ());

    /* Glade can't handle custom menuitems, so set them up manually */
    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "power_menuitem"));
    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (menuitem), hbox);
    image = gtk_image_new_from_icon_name ("system-shutdown", GTK_ICON_SIZE_MENU);
    gtk_widget_show (image);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

    menuitem = GTK_WIDGET (gtk_builder_get_object (builder, "a11y_menuitem"));
    hbox = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (hbox);
    gtk_container_add (GTK_CONTAINER (menuitem), hbox);
    image = gtk_image_new_from_icon_name ("accessibility", GTK_ICON_SIZE_MENU);
    gtk_widget_show (image);
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, TRUE, 0);

    if (!lightdm_get_can_suspend ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "suspend_menuitem")));
    if (!lightdm_get_can_hibernate ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "hibernate_menuitem")));
    if (!lightdm_get_can_restart ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "restart_menuitem")));
    if (!lightdm_get_can_shutdown ())
        gtk_widget_hide (GTK_WIDGET (gtk_builder_get_object (builder, "shutdown_menuitem")));

    user_view = GTK_TREE_VIEW (gtk_builder_get_object (builder, "user_treeview"));
    gtk_tree_view_insert_column_with_attributes (user_view, 0, "Face", gtk_cell_renderer_pixbuf_new(), "pixbuf", 3, NULL);
    gtk_tree_view_insert_column_with_attributes (user_view, 1, "Name", gtk_cell_renderer_text_new(), "text", 1, "weight", 2, NULL);

    if (lightdm_greeter_get_hide_users_hint (greeter))
        start_authentication (NULL);
    else
    {
        load_user_list ();
        gtk_widget_show (GTK_WIDGET (user_view));
    }

    renderer = gtk_cell_renderer_text_new();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (session_combo), renderer, TRUE);
    gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (session_combo), renderer, "text", 0);
    model = gtk_combo_box_get_model (session_combo);
    items = lightdm_get_sessions ();
    for (item = items; item; item = item->next)
    {
        LightDMSession *session = item->data;

        gtk_widget_show (GTK_WIDGET (session_combo));
        gtk_list_store_append (GTK_LIST_STORE (model), &iter);
        gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                            0, lightdm_session_get_name (session),
                            1, lightdm_session_get_key (session),
                            -1);
    }
    set_session (NULL);

    if (g_key_file_get_boolean (config, "greeter", "show-language-selector", NULL))
    {
        gtk_widget_show (GTK_WIDGET (language_combo));

        renderer = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (language_combo), renderer, TRUE);
        gtk_cell_layout_add_attribute (GTK_CELL_LAYOUT (language_combo), renderer, "text", 0);
        model = gtk_combo_box_get_model (language_combo);
        items = lightdm_get_languages ();
        for (item = items; item; item = item->next)
        {
            LightDMLanguage *language = item->data;
            gchar *label;

            label = g_strdup_printf ("%s - %s", lightdm_language_get_name (language), lightdm_language_get_territory (language));

            gtk_widget_show (GTK_WIDGET (language_combo));
            gtk_list_store_append (GTK_LIST_STORE (model), &iter);
            gtk_list_store_set (GTK_LIST_STORE (model), &iter,
                                0, label,
                                1, lightdm_language_get_code (language),
                                -1);
            g_free (label);
        }
        set_language (NULL);
    }

    gtk_builder_connect_signals(builder, greeter);

    gtk_widget_show (GTK_WIDGET (login_window));
    center_window (login_window);

    gtk_widget_show (GTK_WIDGET (panel_window));
    GtkAllocation allocation;
    gtk_widget_get_allocation (GTK_WIDGET (panel_window), &allocation);
    gdk_screen_get_monitor_geometry (gdk_screen_get_default (), gdk_screen_get_primary_monitor (gdk_screen_get_default ()), &monitor_geometry);
    gtk_window_resize (panel_window, monitor_geometry.width, allocation.height);
    gtk_window_move (panel_window, monitor_geometry.x, monitor_geometry.y);

    gtk_widget_show (GTK_WIDGET (login_window));
    gdk_window_focus (gtk_widget_get_window (GTK_WIDGET (login_window)), GDK_CURRENT_TIME);

    gtk_main ();

    return EXIT_SUCCESS;
}
