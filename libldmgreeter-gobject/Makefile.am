lib_LTLIBRARIES = libldmgreeter-gobject.la

libldmgreeter_gobjectinclude_HEADERS = \
	greeter.h \
	language.h \
	layout.h \
	session.h \
	user.h
libldmgreeter_gobjectincludedir=$(includedir)/lightdm-gobject-0/lightdm

libldmgreeter_gobject_la_LIBADD = $(LIBLDMGREETER_GOBJECT_LIBS)
libldmgreeter_gobject_la_CFLAGS = $(LIBLDMGREETER_GOBJECT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

libldmgreeter_gobject_la_SOURCES= \
	greeter.c \
	language.c \
	layout.c \
	session.c \
	user.c

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INTROSPECTION_GIRS = LightDMGreeter-0.gir

LightDMGreeter-0.gir: libldmgreeter-gobject.la
LightDMGreeter_0_gir_INCLUDES = GObject-2.0
LightDMGreeter_0_gir_CFLAGS = $(INCLUDES)
LightDMGreeter_0_gir_LIBS = libldmgreeter-gobject.la
LightDMGreeter_0_gir_FILES = $(libldmgreeter_gobject_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDMGreeter-0.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDMGreeter-0.typelib

endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libldmgreeter-gobject-0.pc

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA) \
	$(gir_DATA) \
	$(typelib_DATA)
