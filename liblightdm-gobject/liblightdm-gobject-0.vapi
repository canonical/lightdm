[CCode (cprefix = "Ldm", lower_case_cprefix = "ldm_", gir_namespace = "LightDM", gir_version = "0", cheader_filename = "lightdm/greeter.h")]
namespace LightDM {
	public class Greeter : GLib.Object {
		public Greeter ();
		public virtual signal void connected ();
		public virtual signal void show_error (string text);
		public virtual signal void show_message (string text);
		public virtual signal void show_prompt (string text);
		public virtual signal void timed_login (string username);
		public virtual signal void authentication_complete ();
		public virtual signal void quit ();
		public bool connect_to_server ();
		public void login (string username);
		public void cancel_authentication ();
		public void cancel_timed_login ();
		public bool get_boolean_property (string name);
		public int get_integer_property (string name);
		public string get_string_property (string name);
		public unowned GLib.List<weak LightDM.Language> get_languages ();
		public unowned GLib.List<weak LightDM.Layout> get_layouts ();
		public unowned GLib.List<weak LightDM.Session> get_sessions ();
		public bool get_user_defaults (string username, out string language, out string layout, out string session);
		public unowned GLib.List<weak LightDM.User> get_users ();
		public void restart ();
		public void shutdown ();
		public void suspend ();
		public void hibernate ();
		public void start_session (string? session);
		public void start_default_session ();
		public void respond (string response);
		public void set_layout (string layout);
		public void login_as_guest ();
		public unowned string default_language { get; }
		public unowned string default_layout { get; }
		public unowned string authentication_user { get; }
		public bool can_hibernate { get; }
		public bool can_restart { get; }
		public bool can_shutdown { get; }
		public bool can_suspend { get; }
		public unowned string default_session { get; set; }
		public unowned string hostname { get; }
		public bool in_authentication { get; }
		public bool is_authenticated { get; }
		public unowned string layout { get; set; }
		public int login_delay { get; }
		public int num_users { get; }
        public unowned string theme { get; }
        public int timed_login_delay { get; }
		public unowned string timed_login_user { get; }
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
