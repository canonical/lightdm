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

dist_man1_MANS = dm-tool.1 \
                 lightdm.1

EXTRA_DIST = apparmor/lightdm-guest-session.in
CLEANFILES = lightdm-guest-session

DISTCLEANFILES = \
	Makefile.in
