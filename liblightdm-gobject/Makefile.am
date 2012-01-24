lib_LTLIBRARIES = liblightdm-gobject-1.la

CLEANFILES =

liblightdm_gobject_1_la_LDFLAGS = -export-symbols-regex \^lightdm_.*
liblightdm_gobject_1_la_LIBADD = $(LIBLIGHTDM_GOBJECT_LIBS)
liblightdm_gobject_1_la_CFLAGS = $(LIBLIGHTDM_GOBJECT_CFLAGS) \
	$(WARN_CFLAGS) \
	-DXSESSIONS_DIR=\"$(datadir)/xsessions\"

mainheader_HEADERS = lightdm.h
mainheaderdir=$(includedir)/lightdm-gobject-1

liblightdm_gobject_1include_HEADERS = \
	lightdm/greeter.h \
	lightdm/system.h \
	lightdm/language.h \
	lightdm/layout.h \
	lightdm/power.h \
	lightdm/session.h \
	lightdm/user.h
liblightdm_gobject_1includedir=$(mainheaderdir)/lightdm

liblightdm_gobject_1_la_SOURCES= \
	greeter.c \
	system.c \
	language.c \
	layout.c \
	power.c \
	session.c \
	user.c \
	$(liblightdm_gobject_1include_HEADERS)

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) --warn-all
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
