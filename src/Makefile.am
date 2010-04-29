bin_PROGRAMS = lightdm

lightdm_SOURCES = \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_BINARY=\"lightdm\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

DISTCLEANFILES = \
	Makefile.in
