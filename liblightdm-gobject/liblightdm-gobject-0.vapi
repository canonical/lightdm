[CCode (cprefix = "Ldm", lower_case_cprefix = "ldm_", gir_namespace = "LightDM", gir_version = "0", cheader_filename = "lightdm/greeter.h")]
namespace LightDM {
	public class Greeter : GLib.Object {
		[CCode (has_construct_function = false)]
		public Greeter ();
		public void cancel_authentication ();
		public void cancel_timed_login ();
		public bool connect_to_server ();
		public unowned string get_authentication_user ();
		public bool get_boolean_property (string name);
		public bool get_can_hibernate ();
		public bool get_can_restart ();
		public bool get_can_shutdown ();
		public bool get_can_suspend ();
		public unowned string get_default_language ();
		public unowned string get_default_layout ();
		public unowned string get_default_session ();
		public unowned string get_hostname ();
		public bool get_in_authentication ();
		public int get_integer_property (string name);
		public bool get_is_authenticated ();
		public unowned GLib.List<weak LightDM.Language> get_languages ();
		public unowned string get_layout ();
		public unowned GLib.List<weak LightDM.Layout> get_layouts ();
		public int get_num_users ();
		public unowned GLib.List<weak LightDM.Session> get_sessions ();
		public string get_string_property (string name);
		public unowned string get_theme ();
		public int get_timed_login_delay ();
		public unowned string get_timed_login_user ();
		public bool get_user_defaults (string username, out string language, out string layout, out string session);
		public unowned GLib.List<weak LightDM.User> get_users ();
		public void hibernate ();
		public void login (string username, string? session, string? language);
		public void login_with_defaults (string username);
		public void provide_secret (string secret);
		public void restart ();
		public void set_layout (string layout);
		public void shutdown ();
		public void start_authentication (string username);
		public void suspend ();
		public string authentication_user { get; }
		public bool can_hibernate { get; }
		public bool can_restart { get; }
		public bool can_shutdown { get; }
		public bool can_suspend { get; }
		public string default_session { get; set; }
		public string hostname { get; }
		public bool in_authentication { get; }
		public bool is_authenticated { get; }
		public string layout { get; set; }
		[NoAccessorMethod]
		public int login_delay { get; }
		public int num_users { get; }
		public string timed_login_user { get; }
		public virtual signal void authentication_complete ();
		public virtual signal void connected ();
		public virtual signal void quit ();
		public virtual signal void show_error (string greeter);
		public virtual signal void show_message (string greeter);
		public virtual signal void show_prompt (string greeter);
		public virtual signal void timed_login (string greeter);
	}
	public class Language : GLib.Object {
		[CCode (has_construct_function = false)]
		public Language (string code);
		public unowned string get_code ();
		public unowned string get_name ();
		public unowned string get_territory ();
		public bool matches (string code);
		public string code { get; construct; }
		public string name { get; }
		public string territory { get; }
	}
	public class Layout : GLib.Object {
		[CCode (has_construct_function = false)]
		public Layout (string name, string short_description, string description);
		public unowned string get_description ();
		public unowned string get_name ();
		public unowned string get_short_description ();
		public string description { get; construct; }
		public string name { get; construct; }
		public string short_description { get; construct; }
	}
	public class Session : GLib.Object {
		[CCode (has_construct_function = false)]
		public Session (string key, string name, string comment);
		public unowned string get_comment ();
		public unowned string get_key ();
		public unowned string get_name ();
		public string comment { get; construct; }
		public string key { get; construct; }
		public string name { get; construct; }
	}
	public class User : GLib.Object {
		[CCode (has_construct_function = false)]
		public User (LightDM.Greeter greeter, string name, string real_name, string image, bool logged_in);
		public unowned string get_display_name ();
		public unowned string get_image ();
		public unowned string get_language ();
		public unowned string get_layout ();
		public bool get_logged_in ();
		public unowned string get_name ();
		public unowned string get_real_name ();
		public unowned string get_session ();
		public string display_name { get; }
		[NoAccessorMethod]
		public LightDM.Greeter greeter { construct; }
		public string image { get; construct; }
		public string language { get; }
		public string layout { get; }
		public bool logged_in { get; construct; }
		public string name { get; construct; }
		public string real_name { get; construct; }
		public string session { get; }
	}
}
