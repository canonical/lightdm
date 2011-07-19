[CCode (cprefix = "LightDM", lower_case_cprefix = "lightdm_", cheader_filename = "lightdm/greeter.h")]
namespace LightDM {
    public class Greeter : GLib.Object {
        public Greeter ();
        public virtual signal void connected ();
        public virtual signal void show_message (string text, MessageType type);
        public virtual signal void show_prompt (string text, PromptType type);
        public virtual signal void timed_login (string username);
        public virtual signal void authentication_complete ();
        public virtual signal void quit ();

        public bool connect_to_server ();

        public unowned string hostname { get; }
        public unowned string theme { get; }
        public bool get_boolean_property (string name);
        public int get_integer_property (string name);
        public string get_string_property (string name);
        public unowned GLib.List<weak LightDM.Language> get_languages ();
        public unowned string default_language { get; }
        public unowned GLib.List<weak LightDM.Layout> get_layouts ();
        public unowned string layout { get; set; }
        public unowned string default_layout { get; }
        public unowned GLib.List<weak LightDM.Session> get_sessions ();
        public unowned string default_session { get; }
        public bool has_guest_session { get; }
        public int num_users { get; }
        public unowned GLib.List<weak LightDM.User> get_users ();
        public unowned LightDM.User get_user_by_name (string username);
        public unowned string timed_login_user { get; }
        public int timed_login_delay { get; }

        public bool can_hibernate { get; }
        public bool can_restart { get; }
        public bool can_shutdown { get; }
        public bool can_suspend { get; }
        public void restart ();
        public void shutdown ();
        public void suspend ();
        public void hibernate ();

        public void cancel_timed_login ();
        public void login (string username);
        public void login_with_user_prompt ();
        public void login_as_guest ();
        public unowned string authentication_user { get; }
        public void respond (string response);
        public bool in_authentication { get; }
        public bool is_authenticated { get; }
        public void cancel_authentication ();
        public void start_session (string? session);
        public void start_default_session ();
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
    public class User : GLib.Object {
        public unowned string display_name { get; }
        public unowned string image { get; }
        public unowned string language { get; }
        public unowned string layout { get; }
        public bool logged_in { get; }
        public unowned string name { get; }
        public unowned string real_name { get; }
        public unowned string session { get; }
    }
}
