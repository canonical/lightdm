SUBDIRS = init

confdir = $(sysconfdir)/lightdm
dist_conf_DATA = lightdm.conf users.conf keys.conf

dbusconfdir = $(sysconfdir)/dbus-1/system.d
dist_dbusconf_DATA = org.freedesktop.DisplayManager.conf

pamdir = $(sysconfdir)/pam.d
dist_pam_DATA = pam/lightdm \
                pam/lightdm-autologin \
                pam/lightdm-greeter

EXTRA_DIST = guest-session.apparmor

apparmor_profiledir = $(sysconfdir)/apparmor.d

install-data-hook:
	install -d $(DESTDIR)$(apparmor_profiledir)
	sed 's!PKGLIBEXECDIR!$(pkglibexecdir)!g' < $(srcdir)/guest-session.apparmor \
		> $(DESTDIR)$(apparmor_profiledir)/lightdm-guest-session

dist_man1_MANS = lightdm.1

DISTCLEANFILES = \
	Makefile.in
