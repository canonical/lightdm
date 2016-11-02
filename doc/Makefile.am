DOC_MODULE = lightdm-gobject-1
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.sgml
DOC_SOURCE_DIR = $(top_srcdir)/liblightdm-gobject

SCANGOBJ_OPTIONS =
SCAN_OPTIONS = --deprecated-guards="LIGHTDM_DISABLE_DEPRECATED" --rebuild-types
MKDB_OPTIONS = --output-format=xml --name-space=LightDM
MKTMPL_OPTIONS =
FIXXREF_OPTIONS = \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/glib \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/gobject \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/gio

HFILE_GLOB = $(top_srcdir)/liblightdm-gobject/lightdm.h
CFILE_GLOB = $(top_srcdir)/liblightdm-gobject/*.c

IGNORE_HFILES = \
	snapd-glib.h 		\
	snapd-enum-types.h

EXTRA_HFILES =

HTML_IMAGES =

content_files =

expand_content_files =

AM_CPPFLAGS = -I$(top_srcdir)/liblightdm-gobject $(LIBLIGHTDM_GOBJECT_CFLAGS)
GTKDOC_LIBS = $(top_builddir)/liblightdm-gobject/liblightdm-gobject-1.la $(LIBLIGHTDM_GOBJECT_LIBS)

include $(top_srcdir)/gtk-doc.make
