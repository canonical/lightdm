# -*- Mode: Automake; indent-tabs-mode: t; tab-width: 4 -*-

noinst_LTLIBRARIES = libcommon.la

libcommon_la_SOURCES = \
	configuration.c \
	configuration.h \
	dmrc.c \
	dmrc.h \
	privileges.c \
	privileges.h \
	user-list.c \
	user-list.h

libcommon_la_CFLAGS = \
	$(WARN_CFLAGS) \
	$(GLIB_CFLAGS) \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DSYSTEM_CONFIG_DIR=\"$(pkgdatadir)/lightdm.conf.d\"

libcommon_la_LIBADD = \
	$(GLIB_LDFLAGS)

DISTCLEANFILES = \
	Makefile.in
