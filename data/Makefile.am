SUBDIRS = init

confdir = $(sysconfdir)/lightdm
dist_conf_DATA = lightdm.conf users.conf keys.conf

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dist_dbusconf_DATA = org.freedesktop.DisplayManager.conf

pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/lightdm \
                pam/lightdm-autologin \
                pam/lightdm-greeter

apparmor/lightdm-guest-session: $(srcdir)/apparmor/lightdm-guest-session.in
	sed -e 's|@pkglibexecdir[@]|$(pkglibexecdir)|g' $< >$@

apparmor_profiledir = $(sysconfdir)/apparmor.d
dist_apparmor_profile_DATA = \
    apparmor/lightdm-guest-session

apparmor_profile_abstractionsdir = $(apparmor_profiledir)/abstractions
dist_apparmor_profile_abstractions_DATA = \
    apparmor/abstractions/lightdm \
    apparmor/abstractions/lightdm_chromium-browser

dist_man1_MANS = lightdm.1 \
                 lightdm-set-defaults.1

CLEANFILES = apparmor/lightdm-guest-session

DISTCLEANFILES = \
	Makefile.in
