lib_LTLIBRARIES = libldmgreeter.la

libldmgreeterinclude_HEADERS = \
	greeter.h \
	language.h \
	layout.h \
	session.h \
	user.h

libldmgreeter_la_LIBADD = $(LIBLDMGREETER_LIBS)
libldmgreeter_la_CFLAGS = $(LIBLDMGREETER_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSION_DIR)\"

libldmgreeter_la_SOURCES= \
	greeter.c \
	language.c \
	layout.c \
	session.c \
	user.c

libldmgreeterincludedir=$(includedir)/lightdm-1.0/lightdm

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INTROSPECTION_GIRS = LightDMGreeter-1.gir

LightDMGreeter-1.gir: libldmgreeter.la
LightDMGreeter_1_gir_INCLUDES = GObject-2.0
LightDMGreeter_1_gir_CFLAGS = $(INCLUDES)
LightDMGreeter_1_gir_LIBS = libldmgreeter.la
LightDMGreeter_1_gir_FILES = $(libldmgreeter_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDMGreeter-1.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDMGreeter-1.typelib

endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libldmgreeter-1.pc

DISTCLEANFILES = \
	Makefile.in \
	libldmgreeter-1.pc \
	$(gir_DATA) \
	$(typelib_DATA)
