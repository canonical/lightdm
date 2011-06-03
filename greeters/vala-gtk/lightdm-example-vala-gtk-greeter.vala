class Greeter
{
    private LightDM.Greeter greeter;
    private Gtk.Entry username_entry;
    private Gtk.Entry password_entry;
    private Gtk.Label message_label;

    public Greeter ()
    {
        greeter = new LightDM.Greeter ();
        greeter.connected.connect (connect_cb);
        greeter.show_prompt.connect (show_prompt_cb);
        greeter.show_message.connect (show_message_cb);
        greeter.show_error.connect (show_message_cb);
        greeter.authentication_complete.connect (authentication_complete_cb);
        greeter.timed_login.connect (timed_login_cb);
        greeter.quit.connect (quit_cb);
    }
    
    public void start ()
    {
        greeter.connect_to_server ();
    }

    private void username_activate_cb (Gtk.Entry entry)
    {
        username_entry.sensitive = false;
        greeter.login (username_entry.text);
    }

    private void password_activate_cb (Gtk.Entry entry)
    {
        password_entry.sensitive = false;
        greeter.provide_secret (password_entry.text);
    }

    private void connect_cb (LightDM.Greeter greeter)
    {
        var display = Gdk.Display.get_default ();
        var screen = display.get_default_screen ();
        
        var window = new Gtk.Window ();
        window.set_default_size (screen.get_width (), screen.get_height ());
        
        var vbox = new Gtk.VBox (false, 0);
        window.add (vbox);
        
        var login_align = new Gtk.Alignment (0.5f, 0.5f, 0.0f, 0.0f);
        vbox.pack_start (login_align, true, true, 0);
        
        var login_vbox = new Gtk.VBox (false, 6);
        login_vbox.border_width = 12;
        login_align.add (login_vbox);

        var logo_image = new Gtk.Image.from_icon_name ("computer", Gtk.IconSize.DIALOG);
        logo_image.pixel_size = 64;
        login_vbox.pack_start (logo_image, false, false, 0);
        login_vbox.pack_start (new Gtk.Label (greeter.hostname), false, false, 0);
        
        message_label = new Gtk.Label ("");
        login_vbox.pack_start (message_label, false, false, 0);
        message_label.no_show_all = true;
        
        username_entry = new Gtk.Entry ();
        login_vbox.pack_start (username_entry, false, false, 0);
        username_entry.activate.connect (username_activate_cb);
        
        password_entry = new Gtk.Entry ();
        password_entry.visibility = false;
        password_entry.sensitive = false;
        login_vbox.pack_start (password_entry, false, false, 0);
        password_entry.activate.connect (password_activate_cb);
        password_entry.no_show_all = true;
        
        window.show_all ();
        username_entry.grab_focus ();
    }

    private void show_prompt_cb (LightDM.Greeter greeter, string text)
    {
        password_entry.show ();
        password_entry.sensitive = true;
        password_entry.grab_focus ();
    }

    private void show_message_cb (LightDM.Greeter greeter, string text)
    {
        message_label.label = text;
        message_label.show ();
    }

    private void authentication_complete_cb (LightDM.Greeter greeter)
    {
        password_entry.hide ();
        password_entry.text = "";
        username_entry.text = "";
        username_entry.sensitive = true;
        username_entry.grab_focus ();
        if (greeter.is_authenticated)
            greeter.start_session_with_defaults ();
        else
        {
            message_label.label = "Failed to authenticate";
            message_label.show ();
        }
    }

    private void timed_login_cb (LightDM.Greeter greeter, string username)
    {
        greeter.start_session_with_defaults (); // FIXME: timed user is not authenticated...
    }

    private void quit_cb (LightDM.Greeter greeter)
    {
        Gtk.main_quit ();
    }
    
    static int main (string[] args)
    {
        Gtk.init (ref args);

        var g = new Greeter ();
        g.start ();

        Gtk.main ();

        return 0;
    }
}
