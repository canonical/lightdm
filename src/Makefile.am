bin_PROGRAMS = lightdm

display-glue.h: display.xml
	$(DBUSBINDINGTOOL) --prefix=display --mode=glib-server --output=$@ $^

lightdm_SOURCES = \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c

BUILT_SOURCES = \
	display-glue.h

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
	$(BUILT_SOURCES) \
	Makefile.in
