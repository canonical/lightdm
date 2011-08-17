AM_CPPFLAGS = \
	$(LIGHTDM_CFLAGS) \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOCALE_DIR=\"$(datadir)/locale\"

libexec_PROGRAMS = \
	lightdm-set-defaults

dist_pkglibexec_SCRIPTS = gdmflexiserver

lightdm_set_defaults_SOURCES = \
	lightdm-set-defaults.c

lightdm_set_defaults_LDADD = \
	$(LIGHTDM_LIBS)

DISTCLEANFILES = \
    Makefile.in

