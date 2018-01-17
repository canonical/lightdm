lib_LTLIBRARIES = liblightdm-gobject-1.la

liblightdm_gobject_1_la_LDFLAGS = -export-symbols-regex \^lightdm_.*
liblightdm_gobject_1_la_LIBADD = \
	$(LIBLIGHTDM_GOBJECT_LIBS) \
	$(top_builddir)/common/libcommon.la
liblightdm_gobject_1_la_CFLAGS = $(LIBLIGHTDM_GOBJECT_CFLAGS) \
	$(WARN_CFLAGS) \
	-I"$(top_srcdir)/common" \
	-DCONFIG_DIR=\"$(sysconfdir)/lightdm\" \
	-DSESSIONS_DIR=\"$(pkgdatadir)/sessions:$(datadir)/xsessions:$(datadir)/wayland-sessions\" \
	-DWAYLAND_SESSIONS_DIR=\"$(datadir)/wayland-sessions\" \
	-DREMOTE_SESSIONS_DIR=\"$(pkgdatadir)/remote-sessions\"

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
LightDM_1_gir_INCLUDES = GObject-2.0 Gio-2.0
LightDM_1_gir_SCANNERFLAGS = --symbol-prefix=lightdm --c-include "lightdm.h"
LightDM_1_gir_LIBS = liblightdm-gobject-1.la
LightDM_1_gir_FILES = $(liblightdm_gobject_1_la_SOURCES)

girdir = $(datadir)/gir-1.0
gir_DATA = LightDM-1.gir

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = LightDM-1.typelib

endif

if ENABLE_VAPIGEN

include $(VAPIGEN_MAKEFILE)

liblightdm-gobject-1.vapi: $(gir_DATA) LightDM-1.metadata LightDM-1-custom.vala

VAPIGEN_VAPIS = liblightdm-gobject-1.vapi

liblightdm_gobject_1_vapi_DEPS = gio-2.0 posix
liblightdm_gobject_1_vapi_METADATADIRS = $(srcdir)
liblightdm_gobject_1_vapi_FILES = $(gir_DATA) $(srcdir)/LightDM-1-custom.vala

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPIGEN_VAPIS) $(srcdir)/liblightdm-gobject-1.deps

endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liblightdm-gobject-1.pc

CLEANFILES = \
	$(gir_DATA) \
	$(typelib_DATA) \
	$(VAPIGEN_VAPIS) \
	$(pkgconfig_DATA)

EXTRA_DIST = \
	LightDM-1.metadata \
	LightDM-1-custom.vala \
	liblightdm-gobject-1.deps
