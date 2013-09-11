SUBDIRS = init

confdir = $(sysconfdir)/lightdm
dist_conf_DATA = lightdm.conf users.conf keys.conf

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dist_dbusconf_DATA = org.freedesktop.DisplayManager.conf

pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/lightdm \
                pam/lightdm-autologin \
                pam/lightdm-greeter

lightdm-guest-session: $(srcdir)/apparmor/lightdm-guest-session.in
	sed -e 's|@pkglibexecdir[@]|$(pkglibexecdir)|g' $< >$@

apparmor_profiledir = $(sysconfdir)/apparmor.d
apparmor_profile_DATA = \
    lightdm-guest-session


dist_man1_MANS = lightdm.1

EXTRA_DIST = apparmor/lightdm-guest-session.in
CLEANFILES = lightdm-guest-session

DISTCLEANFILES = \
	Makefile.in
