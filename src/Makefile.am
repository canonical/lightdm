bin_PROGRAMS = lightdm ldmgreeter

display-glue.h: display.xml
	$(DBUSBINDINGTOOL) --prefix=display --mode=glib-server --output=$@ $^

lightdm_SOURCES = \
	display.c \
	display.h \
	display-manager.c \
	display-manager.h \
	lightdm.c \
	pam-session.c \
	pam-session.h

ldmgreeter_SOURCES = \
	greeter.c \
	greeter.h \
	ldmgreeter.c

BUILT_SOURCES = \
	display-glue.h

lightdm_CFLAGS = \
	$(LIGHTDM_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DGREETER_USER=\"gdm\" \
	-DGREETER_BINARY=\"$(prefix)/bin/ldmgreeter\" \
	-DLIGHTDM_BINARY=\"lightdm\"

lightdm_LDADD = \
	$(LIGHTDM_LIBS) \
	-lpam

ldmgreeter_CFLAGS = \
	$(LIGHTDM_GREETER_CFLAGS) \
	$(WARN_CFLAGS) \
	-DVERSION=\"$(VERSION)\" \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALE_DIR=\"$(localedir)\" \
	-DLIGHTDM_GREETER_BINARY=\"ldmgreeter\"

ldmgreeter_LDADD = \
	$(LIGHTDM_GREETER_LIBS)

DISTCLEANFILES = \
	$(BUILT_SOURCES) \
	Makefile.in
