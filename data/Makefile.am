SUBDIRS = init

confdir = $(sysconfdir)
conf_DATA = lightdm.conf

dbusconfdir = $(DBUS_SYS_DIR)
dbusconf_DATA = org.lightdm.LightDisplayManager.conf

man1_MANS = lightdm.1

EXTRA_DIST = $(conf_DATA) $(dbusconf_DATA) $(man1_MANS)

DISTCLEANFILES = \
	Makefile.in
