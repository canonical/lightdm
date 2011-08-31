[CCode (cprefix = "LightDM", lower_case_cprefix = "lightdm_", cheader_filename = "lightdm.h")]
namespace LightDM {
    public static unowned string get_hostname ();
    public static unowned GLib.List<weak LightDM.Session> get_sessions ();
    public static unowned GLib.List<weak LightDM.Language> get_languages ();
    public static unowned GLib.List<weak LightDM.Layout> get_layouts ();
    public static unowned Language get_language ();
    public static void set_layout (Layout layout);
    public static unowned Layout get_layout ();
    public static bool get_can_suspend ();
    public static bool suspend () throws GLib.Error;
    public static bool get_can_hibernate ();
    public static bool hibernate () throws GLib.Error;
    public static bool get_can_restart ();
    public static bool restart () throws GLib.Error;
    public static bool get_can_shutdown ();
    public static bool shutdown () throws GLib.Error;

    public class Greeter : GLib.Object {
        public Greeter ();
        public signal void show_message (string text, MessageType type);
        public signal void show_prompt (string text, PromptType type);
        public signal void authentication_complete ();
        public signal void autologin_timer_expired ();

        public bool connect_sync () throws GLib.Error;
        public unowned string get_hint (string name);
        public unowned string default_session_hint { get; }
        public bool hide_users_hint { get; }
        public bool has_guest_account_hint { get; }
        public unowned string select_user_hint { get; }
        public bool select_guest_hint { get; }
        public unowned string autologin_user_hint { get; }
        public bool autologin_guest_hint { get; }
        public int autologin_timeout_hint { get; }
        public void cancel_autologin ();
        public void authenticate (string? username = null);
        public void authenticate_as_guest ();
        public void respond (string response);
        public void cancel_authentication ();
        public bool in_authentication { get; }
        public bool is_authenticated { get; }
        public unowned string? authentication_user { get; }
        public void start_session_sync (string? session = null) throws GLib.Error;
    }
    public enum MessageType {
        INFO,
        ERROR
    }
    public enum PromptType {
        QUESTION,
        SECRET
    }
    public class Language : GLib.Object {
        public unowned string code { get; }
        public unowned string name { get; }
        public unowned string territory { get; }
        public bool matches (string code);
    }
    public class Layout : GLib.Object {
        public unowned string description { get; }
        public unowned string name { get; }
        public unowned string short_description { get; }
    }
    public class Session : GLib.Object {
        public unowned string comment { get; }
        public unowned string key { get; }
        public unowned string name { get; }
    }
    public class UserList : GLib.Object {
        public static unowned UserList get_instance ();

        public signal void user_added (User user);
        public signal void user_changed (User user);
        public signal void user_removed (User user);

        public UserList ();
        public int num_users { get; }
        public unowned GLib.List<weak LightDM.User> users { get; }
        public unowned LightDM.User get_user_by_name (string username);
    }
    public class User : GLib.Object {
        public signal void changed ();

        public unowned string display_name { get; }
        public unowned string image { get; }
        public unowned string language { get; }
        public unowned string layout { get; }
        public bool logged_in { get; }
        public unowned string name { get; }
        public unowned string real_name { get; }
        public unowned string home_directory { get; }
        public unowned string session { get; }
    }
}
