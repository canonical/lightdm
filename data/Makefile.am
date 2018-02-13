initdir = ${sysconfdir}/init
dist_init_DATA = init/lightdm.conf

confdir = $(sysconfdir)/lightdm
dist_conf_DATA = lightdm.conf users.conf keys.conf

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dist_dbusconf_DATA = org.freedesktop.DisplayManager.conf

pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/lightdm \
                pam/lightdm-autologin \
                pam/lightdm-greeter

completionsdir = $(datadir)/bash-completion/completions
dist_completions_DATA = bash-completion/dm-tool bash-completion/lightdm

lightdm-guest-session: $(srcdir)/apparmor/lightdm-guest-session.in
	sed -e 's|@libexecdir[@]|$(libexecdir)|g' $< >$@

apparmor_profiledir = $(sysconfdir)/apparmor.d
apparmor_profile_DATA = \
    lightdm-guest-session

apparmor_profile_abstractionsdir = $(apparmor_profiledir)/abstractions
dist_apparmor_profile_abstractions_DATA = \
    apparmor/abstractions/lightdm \
    apparmor/abstractions/lightdm_chromium-browser

accountsservice_interface = org.freedesktop.DisplayManager.AccountsService.xml

dbusdir = $(datadir)/dbus-1/interfaces
dist_dbus_DATA = \
    $(accountsservice_interface)

# AccountsService requires D-Bus interfaces to be symlinked
install-exec-hook:
	$(MKDIR_P) $(DESTDIR)$(datadir)/accountsservice/interfaces
	$(LN_S) ../../dbus-1/interfaces/$(accountsservice_interface) $(DESTDIR)$(datadir)/accountsservice/interfaces/$(accountsservice_interface)

polkitdir = $(datadir)/polkit-1/actions
polkit_in_files = org.freedesktop.DisplayManager.AccountsService.policy.in
polkit_DATA = $(polkit_in_files:.policy.in=.policy)
@INTLTOOL_POLICY_RULE@

dist_man1_MANS = dm-tool.1 \
                 lightdm.1

EXTRA_DIST = apparmor/lightdm-guest-session.in $(polkit_in_files)
CLEANFILES = lightdm-guest-session $(polkit_DATA)
