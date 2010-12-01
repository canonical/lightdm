lib_LTLIBRARIES = liblightdm-gobject.la

liblightdm_gobjectinclude_HEADERS = \
	greeter.h \
	language.h \
	layout.h \
	session.h \
	user.h
liblightdm_gobjectincludedir=$(includedir)/lightdm-gobject-0/lightdm

liblightdm_gobject_la_LIBADD = $(LIBLIGHTDM_GOBJECT_LIBS)
liblightdm_gobject_la_CFLAGS = $(LIBLIGHTDM_GOBJECT_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

liblightdm_gobject_la_SOURCES= \
	greeter.c \
	language.c \
	layout.c \
	session.c \
	user.c \
	$(liblightdm_gobjectinclude_HEADERS)

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INTROSPECTION_GIRS = LightDMGreeter-0.gir

LightDMGreeter-0.gir: liblightdm-gobject.la
LightDMGreeter_0_gir_INCLUDES = GObject-2.0
LightDMGreeter_0_gir_CFLAGS = $(INCLUDES) --identifier-prefix=Ldm
LightDMGreeter_0_gir_LIBS = liblightdm-gobject.la
LightDMGreeter_0_gir_FILES = $(liblightdm_gobject_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDMGreeter-0.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDMGreeter-0.typelib

endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-gobject-0.pc

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA) \
	$(gir_DATA) \
	$(typelib_DATA)
