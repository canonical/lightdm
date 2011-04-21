SUBDIRS = lightdm

lib_LTLIBRARIES = liblightdm-gobject-0.la

CLEANFILES =

liblightdm_gobject_0_la_LIBADD = $(LIBLIGHTDM_GOBJECT_LIBS)
liblightdm_gobject_0_la_CFLAGS = $(LIBLIGHTDM_GOBJECT_CFLAGS) \
	$(WARN_CFLAGS) \
	-DXSESSIONS_DIR=\"$(XSESSIONS_DIR)\"

liblightdm_gobject_0_la_SOURCES= \
	greeter.c \
	greeter-protocol.h \
	language.c \
	layout.c \
	session.c \
	user.c \
	lightdm/greeter.h \
	lightdm/language.h \
	lightdm/layout.h \
	lightdm/session.h \
	lightdm/user.h

if HAVE_INTROSPECTION

-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)

INTROSPECTION_GIRS = LightDM-0.gir

LightDM-0.gir: liblightdm-gobject-0.la
LightDM_0_gir_INCLUDES = GObject-2.0
LightDM_0_gir_CFLAGS = $(INCLUDES) --identifier-prefix=Ldm
LightDM_0_gir_LIBS = liblightdm-gobject-0.la
LightDM_0_gir_FILES = $(liblightdm_gobject_0_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDM-0.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDM-0.typelib

endif

vapidir = $(datadir)/vala/vapi
dist_vapi_DATA = liblightdm-gobject-0.vapi

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-gobject-0.pc

DISTCLEANFILES = \
	Makefile.in \
	$(pkgconfig_DATA) \
	$(gir_DATA) \
	$(typelib_DATA)
