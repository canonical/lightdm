SUBDIRS = lightdm

lib_LTLIBRARIES = liblightdm-gobject-1.la

CLEANFILES =

liblightdm_gobject_1_la_LIBADD = $(LIBLIGHTDM_GOBJECT_LIBS)
liblightdm_gobject_1_la_CFLAGS = $(LIBLIGHTDM_GOBJECT_CFLAGS) \
	$(WARN_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

liblightdm_gobject_1_la_SOURCES= \
	greeter.c \
	language.c \
	layout.c \
	session.c \
	user.c \
	user-private.h \
	lightdm/greeter.h \
	lightdm/language.h \
	lightdm/layout.h \
	lightdm/session.h \
	lightdm/user.h

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INTROSPECTION_GIRS = LightDM-1.gir

LightDM-1.gir: liblightdm-gobject-1.la
LightDM_1_gir_INCLUDES = GObject-2.0
LightDM_1_gir_CFLAGS = $(INCLUDES) --identifier-prefix=LightDM
LightDM_1_gir_LIBS = liblightdm-gobject-1.la
LightDM_1_gir_FILES = $(liblightdm_gobject_1_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDM-1.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDM-1.typelib

endif

vapidir = $(datadir)/vala/vapi
dist_vapi_DATA = liblightdm-gobject-1.vapi

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-gobject-1.pc

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA) \
	$(gir_DATA) \
	$(typelib_DATA)
