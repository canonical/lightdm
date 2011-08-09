AM_CPPFLAGS = \
	$(LIGHTDM_CFLAGS) \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DLOCALE_DIR=\"$(datadir)/locale\"	\
	$(NULL)

libexec_PROGRAMS = \
	lightdm-set-defaults \
	$(NULL)

lightdm_set_defaults_SOURCES =	\
	lightdm-set-defaults.c	\
	$(NULL)

lightdm_set_defaults_LDADD =	\
	$(LIGHTDM_LIBS)	\
	$(NULL)

DISTCLEANFILES = \
    Makefile.in

