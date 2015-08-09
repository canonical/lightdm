namespace LightDM {
	[CCode (type_id = "lightdm_user_list_get_type ()")]
	public class UserList : GLib.Object {
		public unowned GLib.List<LightDM.User> users { get; }
	}
}
